"""Actual OFX rendering with missing host capabilities, failures and recovery."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys

CASES = ('off-no-metadata', 'zero-no-metadata', 'active-no-fps', 'manual-fps',
         'unmapped-fps', 'invalid-fps', 'external-preview', 'external-export',
         'notify-failure', 'recover-external', 'external-bounds', 'fetch-failure',
         'reference-failure', 'proxy-format', 'frame-sequences', 'cancel-recover')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--plugin', required=True, type=Path)
parser.add_argument('--sdk', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--case', choices=CASES)
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)

if not args.case:
    reports = []
    for case in CASES:
        path = args.output.parent / ('fusion-' + case + '.json')
        run = subprocess.run([sys.executable, str(Path(__file__).resolve()), '--plugin', str(args.plugin),
                              '--sdk', str(args.sdk), '--output', str(path), '--case', case],
                             capture_output=True, text=True, timeout=40)
        if run.returncode:
            raise RuntimeError(f'{case}: {run.stdout}\n{run.stderr}')
        reports.append(json.loads(path.read_text(encoding='utf-8')))
    result = {'PluginSHA256': hashlib.sha256(args.plugin.read_bytes()).hexdigest().upper(),
              'Passed': True, 'Cases': reports, 'NativeResolveTested': False}
    args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'Passed': True, 'Cases': len(reports), 'PluginSHA256': result['PluginSHA256']}, indent=2))
    sys.exit(0)

case = args.case
log = Path(os.environ['LOCALAPPDATA']) / 'ResolveDlss5/ResolveDlss5.log'
offset = log.stat().st_size if log.exists() else 0
text = (Path(__file__).with_name('OfxHarness.py')).read_text(encoding='utf-8-sig')
text = text.split("af = objects[params['nrAntiFlickerMode']]")[0]
text = text.replace('objects, callbacks, suites, buffers, fetches, messages, missing = {}, [], {}, [], [], [], []',
    'objects, callbacks, suites, buffers, fetches, messages, missing = {}, [], {}, [], [], [], []\nproperty_reads = []\nfaults = {}')
text = text.replace('        array = objects[h].get(key.decode(), [])',
    "        property_reads.append([h, key.decode()])\n        if key.decode() in faults.get('properties', []) and objects[h].get('OfxPropType') != [b'OfxTypeImage']: return 5\n        array = objects[h].get(key.decode(), [])", 1)
text = text.replace("    name = objects[clip]['OfxPropName'][0].decode()\n    fetches.append([name, time])",
    "    name = objects[clip]['OfxPropName'][0].decode()\n    if name == 'Source' and (faults.get('source') or (faults.get('reference') and time < 10)): return 1\n    fetches.append([name, time])")
text = text.replace("    out[0] = value\n    return 0\nsuites[b'OfxImageEffectSuite'",
    "    if name == 'MotionVectors' and faults.get('bounds'): put(value, 'OfxImagePropBounds', [0, 0, w+1, h])\n    if name == 'Source' and faults.get('format'): put(value, 'OfxImageEffectPropComponents', b'OfxImageComponentRGB')\n    out[0] = value\n    return 0\nsuites[b'OfxImageEffectSuite'", 1)
text = text.replace("'abort': cb([P], lambda h: 0)", "'abort': cb([P], lambda h: int(faults.get('abort', False)))")
text = text.replace("lambda h, typ, name, text: (messages.append(text.decode()), 0)[1]",
    "lambda h, typ, name, text: (messages.append(text.decode()), 5 if faults.get('notify') else 0)[1]")
sys.argv = ['OfxHarness.py', '--plugin', str(args.plugin), '--sdk', str(args.sdk), '--output', str(args.output)]
ns = {'__name__': 'fusion_contract_worker'}
exec(compile(text, str(Path(__file__).with_name('OfxHarness.py')), 'exec'), ns)
ns['action']('OfxActionCreateInstance', ns['effect'])
faults, objects, params, clips = ns['faults'], ns['objects'], ns['params'], ns['clips']

def set_param(name, value):
    ns['values'][params[name]] = value
    props = ns['handle']({'OfxPropName': [name.encode()], 'OfxPropType': [b'OfxTypeParameter'],
        'OfxPropChangeReason': [b'OfxChangeUserEdited'], 'OfxPropTime': [10.], 'OfxImageEffectPropRenderScale': [1., 1.]})
    ns['action']('OfxActionInstanceChanged', ns['effect'], props)

def connect_motion(connected):
    ns['put'](clips['MotionVectors'], 'OfxImageClipPropConnected', int(connected))
    props = ns['handle']({'OfxPropName': [b'MotionVectors'], 'OfxPropType': [b'OfxTypeClip'],
        'OfxPropChangeReason': [b'OfxChangeUserEdited'], 'OfxPropTime': [10.], 'OfxImageEffectPropRenderScale': [1., 1.]})
    ns['action']('OfxActionInstanceChanged', ns['effect'], props)

def render(time=10., preview=1):
    for i in range(len(ns['destination'])): ns['destination'][i] = 0
    props = ns['handle']({'OfxPropTime': [time], 'OfxImageEffectPropRenderWindow': [0, 0, ns['w'], ns['h']],
        'OfxImageEffectPropRenderScale': [1., 1.], 'OfxImageEffectPropFieldToRender': [b'OfxFieldNone'],
        'OfxImageEffectPropInteractiveRenderStatus': [preview], 'OfxImageEffectPropSequentialRenderStatus': [1]})
    status = ns['entry'](b'OfxImageEffectActionRender', ns['effect'], props, None)
    value = list(ns['destination'])
    assert all(math.isfinite(v) for v in value)
    return status, value

def processed(time=10., preview=1):
    status, value = render(time, preview)
    assert status == 0, (case, status, ns['messages'])
    assert value != list(ns['source']), 'NR correction missing'
    assert value[3::4] == list(ns['source'])[3::4]
    return value

set_param('nrOpticalFlowMethod', 0)
set_param('nrAntiFlickerMode', 0)
statuses = []
if case in ('off-no-metadata', 'zero-no-metadata', 'active-no-fps', 'manual-fps'):
    faults['properties'] = ['OfxImageEffectPropFrameRate', 'OfxImageEffectPropUnmappedFrameRate', 'OfxImagePropPixelAspectRatio']
    # Image PAR is supplied independently; only the clip/effect properties are absent.
    if case != 'off-no-metadata': set_param('nrAntiFlickerMode', 1)
    if case == 'zero-no-metadata': set_param('nrAntiFlickerStrength', 0.)
    if case == 'manual-fps': set_param('nrAntiFlickerFrameRate', 60.)
    processed()
    processed(11.)
    fps_reads = [v for v in ns['property_reads'] if v[1] in ('OfxImageEffectPropFrameRate', 'OfxImageEffectPropUnmappedFrameRate')]
    if case != 'active-no-fps': assert not fps_reads, fps_reads
    else: assert any('Anti-flicker needs a valid media frame rate' in m for m in ns['messages'])
elif case in ('unmapped-fps', 'invalid-fps'):
    set_param('nrAntiFlickerMode', 1)
    ns['put'](clips['Source'], 'OfxImageEffectPropUnmappedFrameRate', 60.)
    if case == 'unmapped-fps': faults['properties'] = ['OfxImageEffectPropFrameRate']
    else: ns['put'](clips['Source'], 'OfxImageEffectPropFrameRate', float('nan'))
    processed()
    processed(11.)
elif case in ('external-preview', 'external-export', 'notify-failure', 'recover-external', 'external-bounds'):
    set_param('nrExternalMotion', 1)
    if case == 'external-bounds': connect_motion(True); faults['bounds'] = True
    if case == 'notify-failure': faults['notify'] = True
    status, value = render(preview=int(case != 'external-export'))
    statuses.append(status)
    assert status == (1 if case == 'external-export' else 0), (status, ns['messages'])
    assert value == (list(ns['source']) if case != 'external-export' else [0.] * len(value))
    assert any(('MotionVectors is not connected' if case != 'external-bounds' else 'History image bounds changed') in m for m in ns['messages']), ns['messages']
    if case != 'external-bounds': assert ns['values'][params['nrMotionStatus']] == b'Connect MotionVectors'
    if case == 'recover-external':
        connect_motion(True)
        assert ns['values'][params['nrMotionStatus']] == b'External MV connected'
        processed()
        set_param('nrExternalMotion', 0)
        processed(11.)
elif case in ('fetch-failure', 'reference-failure', 'proxy-format'):
    if case == 'reference-failure': faults['reference'] = True; set_param('nrOpticalFlowMethod', 1)
    if case == 'fetch-failure': faults['source'] = True
    if case == 'proxy-format': faults['format'] = True
    status, value = render()
    statuses.append(status)
    assert status == (0 if case == 'reference-failure' else 1000), (status, ns['messages'])
    assert ns['messages'], 'Original format/fetch error missing'
    faults.clear()
    set_param('nrOpticalFlowMethod', 0)
    processed()
elif case == 'frame-sequences':
    set_param('nrAntiFlickerMode', 1)
    set_param('nrAntiFlickerFrameRate', 60.)
    for time in (10., 11.):
        props = ns['handle']({'OfxImageEffectPropFrameRange': [time, time], 'OfxImageEffectPropFrameStep': [1.],
            'OfxImageEffectPropRenderScale': [1., 1.], 'OfxPropIsInteractive': [1],
            'OfxImageEffectPropInteractiveRenderStatus': [0], 'OfxImageEffectPropSequentialRenderStatus': [0]})
        ns['action']('OfxImageEffectActionBeginSequenceRender', ns['effect'], props)
        processed(time, preview=0)
        ns['action']('OfxImageEffectActionEndSequenceRender', ns['effect'], props)
elif case == 'cancel-recover':
    faults['abort'] = True
    status, value = render()
    assert status == 1 and value == [0.] * len(value)
    statuses.append(status)
    faults.clear()
    processed()

ns['action']('OfxActionDestroyInstance', ns['effect'])
ns['action']('OfxActionUnload')
with log.open('rb') as stream:
    stream.seek(offset)
    diagnostics = stream.read().decode('utf-8', errors='replace')
if case in ('manual-fps', 'unmapped-fps', 'invalid-fps', 'frame-sequences'):
    weights = [float(v) for v in re.findall(r'af-weight=([0-9.]+)', diagnostics)]
    assert weights and abs(weights[-1] - math.exp(-1 / 60 / .08)) < 1e-5, (case, weights)
if case in ('external-preview', 'external-export', 'notify-failure'):
    assert 'Render failed: external-motion: External motion input is enabled but MotionVectors is not connected' in diagnostics
report = {'Case': case, 'Passed': True, 'Statuses': statuses, 'Messages': ns['messages'],
          'PluginSHA256': hashlib.sha256(args.plugin.read_bytes()).hexdigest().upper(),
          'NativeResolveTested': False}
args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
args.output.with_suffix('.log').write_text(diagnostics, encoding='utf-8')
