"""Probe unsaved Fusion frames; never load/save a project or change host preferences."""
import argparse
import json
import os
from pathlib import Path
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--script-lib', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
os.environ['RESOLVE_SCRIPT_LIB'] = args.script_lib
sys.path.insert(0, str(Path(os.environ['PROGRAMDATA']) / 'Blackmagic Design/DaVinci Resolve/Support/Developer/Scripting/Modules'))
import DaVinciResolveScript as dvr

resolve = dvr.scriptapp('Resolve')
if resolve is None:
    raise RuntimeError('Start a separate headless Resolve verification session first')
comp = resolve.Fusion().NewComp()
assert comp is not None
frame_root = args.output.parent / 'native-frames'
frame_root.mkdir(parents=True, exist_ok=True)
comp.Lock()
try:
    source = comp.AddTool('FastNoise')
    nr = comp.AddTool('ofx.com.saog.resolve.dlss5')
    saver = comp.AddTool('Saver')
    assert source is not None and nr is not None and saver is not None
    source.SetInput('UseFrameFormatSettings', 0)
    source.SetInput('Width', 480)
    source.SetInput('Height', 270)
    assert nr.ConnectInput('Source', source)
    assert saver.ConnectInput('Input', nr)
    saver.SetInput('OutputFormat', 'OpenEXRFormat')
    nr.SetInput('nrOpticalFlowMethod', 0)
    nr.SetInput('nrExternalMotion', 0)
    nr.SetInput('nrPassCount', 0)
    nr.SetInput('nrInputEncoding', 1)
    comp.SetAttrs({'COMPN_GlobalStart': 0, 'COMPN_GlobalEnd': 2})
finally:
    comp.Unlock()
try:
    result = {'HostVersion': resolve.GetVersionString(), 'SyntheticUnsavedFusionOnly': True,
              'ProjectSavedOrLoaded': False, 'TimelineExportAccepted': False, 'Frames': []}
    for name, changes, frame in [
        ('nr', {}, 0), ('protection-edit', {'nrHueProtection': .3, 'nrCompression': .5}, 0),
        ('frequency-edit', {'nrLowFrequency': .8, 'nrHighFrequency': 1.2}, 0),
        ('next-frame', {}, 1),
    ]:
        comp.Lock()
        try:
            target = (frame_root / f'{name}.exr').resolve()
            assert not target.exists(), f'Preserve existing diagnostic output: {target}'
            saver.SetInput('Clip', str(target))
            for key, value in changes.items():
                assert nr.SetInput(key, value) is not False
        finally:
            comp.Unlock()
        start = time.perf_counter()
        render_result = comp.Render(True, {'Start': frame, 'End': frame})
        elapsed = (time.perf_counter() - start) * 1000
        files = list(frame_root.glob(f'{name}*.exr'))
        assert render_result and files and all(path.stat().st_size > 0 for path in files), f'No Fusion output for {name}'
        result['Frames'].append({'Name': name, 'Time': frame, 'WallMs': elapsed,
                                 'RenderReturned': render_result,
                                 'Files': [{'Path': str(path), 'Bytes': path.stat().st_size} for path in files]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2, default=str), flush=True)
except Exception as error:
    result['NativePreviewRenderObserved'] = False
    result['Error'] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, default=str), encoding='utf-8')
    raise
finally:
    comp.Close()
