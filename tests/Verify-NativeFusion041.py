"""Create an isolated owned Resolve project and compare actual timeline exports."""
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import uuid

import numpy as np
import cv2
from PIL import Image

root = Path(__file__).resolve().parent.parent
out = root / 'tests/results/v0.4.1-fusion/native'
out.mkdir(parents=True, exist_ok=True)
os.environ['RESOLVE_SCRIPT_LIB'] = r'D:\Program Files\Blackmagic Design\DaVinci Resolve\fusionscript.dll'
sys.path.insert(0, r'C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\Developer\Scripting\Modules')
import DaVinciResolveScript as dvr

deadline = time.monotonic() + 50
resolve = dvr.scriptapp('Resolve')
while resolve is None and time.monotonic() < deadline:
    time.sleep(1)
    resolve = dvr.scriptapp('Resolve')
if resolve is None:
    raise RuntimeError('Owned Resolve verification session did not connect')
pm = resolve.GetProjectManager()
previous = pm.GetCurrentProject()
if previous and previous.GetName() and not pm.CloseProject(previous):
    # Resolve leaves a transient empty "Untitled Project" at startup and cannot
    # close it. A unique CreateProject replaces that empty session safely.
    if previous.GetName() != 'Untitled Project' or previous.GetTimelineCount() or previous.GetMediaPool().GetRootFolder().GetClipList():
        raise RuntimeError('Could not close the startup project without saving; no test edits applied')
name = 'DLSS5_041_Fusion_' + uuid.uuid4().hex[:8]
project = pm.CreateProject(name)
assert project and project.GetName() == name
report = {'Version': resolve.GetVersionString(), 'OwnedProject': name, 'UserProjectSaveRequested': False,
          'ParametersPassed': False, 'ActualNrTimelineExportPassed': False, 'Cases': []}
log = Path(os.environ['LOCALAPPDATA']) / 'ResolveDlss5/ResolveDlss5.log'
offset = log.stat().st_size

def save_report():
    (out / 'native-verification.json').write_text(json.dumps(report, indent=2, default=str), encoding='utf-8')

try:
    for key, value in [('timelineResolutionWidth', '640'), ('timelineResolutionHeight', '360'),
                       ('timelineFrameRate', '60')]:
        assert project.SetSetting(key, value), key
    pool = project.GetMediaPool()
    y, x = np.mgrid[:360, :640]
    data = np.stack([((x * 17 + y * 31 + c * 7) % 83) / 83 * 160 + 35 for c in range(3)], axis=2).astype(np.uint8)
    source_path = out / 'generated-source.png'
    Image.fromarray(data).save(source_path)
    clips = pool.ImportMedia([str(source_path.resolve())])
    assert clips
    timeline = pool.CreateEmptyTimeline('Fusion_041_Verification')
    assert timeline and project.SetCurrentTimeline(timeline)
    items = pool.AppendToTimeline([{'mediaPoolItem': clips[0], 'startFrame': 0, 'endFrame': 3, 'mediaType': 1}])
    assert items
    item = items[0]
    comp = item.AddFusionComp()
    assert comp
    resolve.OpenPage('fusion')
    timeline.SetCurrentTimecode(timeline.GetStartTimecode())
    comp.Lock()
    try:
        tools = comp.GetToolList(False) or {}
        source = next(t for t in tools.values() if (t.GetAttrs() or {}).get('TOOLS_RegID') == 'MediaIn')
        destination = next(t for t in tools.values() if (t.GetAttrs() or {}).get('TOOLS_RegID') == 'MediaOut')
        nr = comp.AddTool('ofx.com.saog.resolve.dlss5')
        assert nr and nr.ConnectInput('Source', source) and destination.ConnectInput('Input', nr)
        nr.SetInput('nrOpticalFlowMethod', 0)
        nr.SetInput('nrExternalMotion', 0)
        nr.SetInput('nrAntiFlickerFrameRate', 60)
        inputs = {p.GetAttrs()['INPS_ID']: p.GetAttrs() for p in nr.GetInputList().values()}
        assert 'nrMotionStatus' in inputs and 'nrAntiFlickerFrameRate' in inputs
        assert nr.GetInput('nrAntiFlickerMode') == 0 and nr.GetInput('nrAntiFlickerFrameRate') == 60
        report['ParametersPassed'] = True
        report['MotionStatus'] = nr.GetInput('nrMotionStatus')
        report['CompFrameRate'] = comp.GetPrefs('Comp.FrameFormat.Rate')
        comp_path = out / 'owned-fusion.comp'
        assert comp.Save(str(comp_path.resolve()))
    finally:
        comp.Unlock()
    names = item.GetFusionCompNameList()
    report['FusionCompNames'] = names
    comp = item.LoadFusionCompByName(names[-1])
    assert comp
    nr = next(t for t in comp.GetToolList(False).values() if 'dlss5' in str(t.GetAttrs().get('TOOLS_RegID', '')))
    codecs = project.GetRenderCodecs('tif')
    report['TiffCodecs'] = codecs
    codec = next((v for v in codecs.values() if v == 'RGB16'), next(iter(codecs.values())))
    assert project.SetCurrentRenderFormatAndCodec('tif', codec)

    def export(case, enabled, passes=0, af=0, external=0, frames=1, expected_failure=False):
        assert pm.GetCurrentProject().GetName() == name, 'Owned project changed'
        active = item.GetFusionCompByName(names[-1])
        active_nr = next(t for t in active.GetToolList(False).values() if 'dlss5' in str(t.GetAttrs().get('TOOLS_RegID', '')))
        active.Lock()
        try:
            for param, value in [('nrEnabled', enabled), ('nrPassCount', passes), ('nrAntiFlickerMode', af),
                                 ('nrExternalMotion', external)]:
                active_nr.SetInput(param, value, 0)
            active_nr.SetInput('nrOutputView', nr.GetInput('nrOutputView', 0), 0)
            active_nr.SetInput('nrAntiFlickerFrameRate', nr.GetInput('nrAntiFlickerFrameRate'), 0)
        finally:
            active.Unlock()
        assert pm.SaveProject(), 'Owned graph save failed'
        resolve.OpenPage('deliver')
        folder = out / case
        folder.mkdir(exist_ok=True)
        start = timeline.GetStartFrame()
        assert project.SetRenderSettings({'TargetDir': str(folder.resolve()), 'CustomName': case,
            'SelectAllFrames': False, 'MarkIn': start, 'MarkOut': start + frames - 1,
            'ExportVideo': True, 'ExportAudio': False})
        job = project.AddRenderJob()
        assert job and project.StartRendering(job)
        deadline = time.monotonic() + 40
        while project.IsRenderingInProgress() and time.monotonic() < deadline:
            time.sleep(.25)
        if project.IsRenderingInProgress():
            project.StopRendering()
            raise RuntimeError('Owned timeline render timed out')
        status = project.GetRenderJobStatus(job)
        files = sorted(folder.glob('*.tif')) + sorted(folder.glob('*.tiff'))
        entry = {'Case': case, 'Status': status, 'Files': [str(p) for p in files], 'ExpectedFailure': expected_failure,
                 'InputsAtZero': {k: active_nr.GetInput(k, 0) for k in ('nrEnabled','nrPassCount','nrOutputView','nrExternalMotion')}}
        report['Cases'].append(entry)
        save_report()
        print(json.dumps({'Case': case, 'Status': status, 'Files': len(files)}, default=str), flush=True)
        succeeded = status.get('JobStatus') in ('Complete', '完成') and status.get('CompletionPercentage') == 100
        if expected_failure:
            assert not succeeded, 'Invalid external motion export incorrectly succeeded'
            return None
        assert succeeded and files, entry
        return cv2.imread(str(files[0]), cv2.IMREAD_UNCHANGED)

    baseline = export('baseline', 0)
    processed = export('nr-one', 1)
    difference = np.abs(processed.astype(np.float64) - baseline.astype(np.float64))
    report['ChangedSamples'] = int(np.count_nonzero(difference))
    report['MaxDifference'] = float(difference.max())
    if report['ChangedSamples'] <= 100:
        nr.SetInput('nrOutputView', 1)
        visible = export('difference-view', 1)
        report['DifferenceViewChangedSamples'] = int(np.count_nonzero(visible != baseline))
        report['DifferenceViewStd'] = float(np.std(visible))
        nr.SetInput('nrOutputView', 0)
    report['ActualNrTimelineExportPassed'] = report['ChangedSamples'] > 100
    report['DiagnosticOutputExportPassed'] = report.get('DifferenceViewChangedSamples', 0) > 100
    # A zero correction from this synthetic source does not establish NR image
    # acceptance; retain it as unresolved while testing render/error contracts.
    export('nr-three', 1, passes=2)
    nr.SetInput('nrAntiFlickerFrameRate', 0)
    export('af-auto-rate', 1, af=1, frames=3)
    nr.SetInput('nrAntiFlickerFrameRate', 60)
    for mode in range(1, 5):
        export('af-' + str(mode), 1, af=mode, frames=3)
    export('external-missing', 1, external=1, expected_failure=True)
    export('recovered-none', 1)
    pm.SaveProject()  # Only this newly created owned test project.
    report['ProjectExported'] = pm.ExportProject(name, str((out / 'owned-test-project.drp').resolve()))
    report['RenderAndErrorContractsPassed'] = True
    report['Passed'] = report['ActualNrTimelineExportPassed']
except Exception as error:
    report['Error'] = str(error)
    report['Passed'] = False
    raise
finally:
    with log.open('rb') as stream:
        stream.seek(offset)
        (out / 'native-plugin.log').write_bytes(stream.read())
    report['PluginSHA256'] = hashlib.sha256(Path(r'C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle\Contents\Win64\ResolveDlss5.ofx').read_bytes()).hexdigest().upper()
    save_report()
    if pm.GetCurrentProject() and pm.GetCurrentProject().GetName() == name:
        pm.CloseProject(project)
    resolve.Quit()
