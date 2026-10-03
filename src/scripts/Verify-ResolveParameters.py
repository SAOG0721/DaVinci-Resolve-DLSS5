"""Create an unsaved Fusion test comp in a running Resolve; never render/save a project."""
import argparse
import json
import os
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--script-lib', required=True, help='Installed Resolve fusionscript.dll')
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
os.environ['RESOLVE_SCRIPT_LIB'] = args.script_lib
sys.path.insert(0, str(Path(os.environ['PROGRAMDATA']) / 'Blackmagic Design/DaVinci Resolve/Support/Developer/Scripting/Modules'))
import DaVinciResolveScript as dvr

resolve = dvr.scriptapp('Resolve')
if resolve is None:
    raise RuntimeError('Start a separate headless Resolve verification session first')
comp = resolve.Fusion().NewComp()
if comp is None:
    raise RuntimeError('Unable to create the temporary unsaved composition')
comp.Lock()
try:
    tool = comp.AddTool('ofx.com.saog.resolve.dlss5')
    if tool is None:
        raise RuntimeError('OFX DescribeInContext/CreateInstance did not succeed')
    inputs = {item.GetAttrs()['INPS_ID']: item.GetAttrs() for item in tool.GetInputList().values()}
    nr_inputs = {name: value for name, value in inputs.items() if name.startswith('nr')}
    group_ids = {name for name, value in nr_inputs.items() if value.get('INPID_InputControl') == 'NestControl'}
    controls = {name: value for name, value in nr_inputs.items() if name not in group_ids}
    assert 'nrOpticalFlow' in group_ids, 'Missing optical flow group'
    assert 'nrHistory' not in group_ids and 'nrGuidance' not in group_ids, 'Legacy groups still exposed'
    for name in ('nrExternalMotion','nrOpticalFlowMethod','nrAmdFlowQuality','nrNvidiaFlowQuality',
                 'nrExternalXChannel','nrExternalYChannel','nrExternalUnits','nrExternalYUp',
                 'nrExternalScaleX','nrExternalScaleY'):
        assert name in controls, f'Missing optical flow control: {name}'
    assert 'MotionVectors' in inputs, 'Fusion external motion connector missing'
    vectors=comp.AddTool('Background')
    assert vectors is not None and tool.ConnectInput('MotionVectors',vectors), 'External motion link rejected'
    motion_input=next(item for item in tool.GetInputList().values() if item.GetAttrs()['INPS_ID']=='MotionVectors')
    assert motion_input and motion_input.GetConnectedOutput(), 'External motion input is not connected'
    assert tool.GetInput('nrOpticalFlowMethod',0)==2, 'Default NVOF changed'
    assert tool.GetInput('nrNvidiaFlowQuality',0)==2, 'Default NVOF Quality changed'
    tool.SetInput('nrOpticalFlowMethod',1,0)
    tool.SetInput('nrAmdFlowQuality',0,0)
    tool.SetInput('nrOpticalFlowMethod',2,0)
    tool.SetInput('nrNvidiaFlowQuality',4,0)
    tool.SetInput('nrExternalMotion',1,0)
    tool.SetInput('nrExternalUnits',1,0)
    tool.SetInput('nrExternalYUp',1,0)
    tool.SetInput('nrExternalScaleX',2,0)
    tool.SetInput('nrExternalMotion',0,0)
    assert tool.GetInput('nrExternalScaleX',0)==2
    assert tool.GetInput('nrNvidiaFlowQuality',0)==4
    assert controls['nrInputEncoding']['INPN_MaxAllowed'] == 6, 'Missing color encodings'
    assert controls['nrResidualStrength']['INPN_MaxAllowed'] == 2, 'Correction range changed'
    assert tool.GetInput('nrSkinStructure', 0) == 1, 'Legacy Pass 1 default changed'
    assert tool.GetInput('nrPass2Skin', 0) == 0, 'New Pass 2 default changed'
    assert tool.GetInput('nrPass3Skin', 0) == 0, 'New Pass 3 default changed'
    # Hiding a pass must preserve its settings across 1 -> 3 -> 1 -> 3 changes.
    tool.SetInput('nrPassCount', 2, 0)
    tool.SetInput('nrPass2Intensity', 1.25, 0)
    tool.SetInput('nrPass3Intensity', 0.75, 0)
    tool.SetInput('nrPassCount', 0, 0)
    tool.SetInput('nrPassCount', 2, 0)
    assert tool.GetInput('nrPass2Intensity', 0) == 1.25
    assert tool.GetInput('nrPass3Intensity', 0) == 0.75
    assert tool.GetInput('nrPassCount', 0) == 2
    assert tool.GetInput('nrShowAdvanced', 0) == 0
    tool.SetInput('nrShowAdvanced', 1, 0)
    tool.SetInput('nrHueProtection', 0.65, 0)
    tool.SetInput('nrShowAdvanced', 0, 0)
    tool.SetInput('nrShowAdvanced', 1, 0)
    assert abs(tool.GetInput('nrHueProtection', 0) - 0.65) < 1e-6
    tool.SetInput('nrInputEncoding', 3, 0)
    tool.SetInput('nrReferenceWhite', 400, 0)
    tool.SetInput('nrPeakNits', 1200, 0)
    tool.SetInput('nrInputEncoding', 4, 0)
    tool.SetInput('nrInputEncoding', 5, 0)
    assert tool.GetInput('nrReferenceWhite', 0) == 400
    assert tool.GetInput('nrPeakNits', 0) == 1200
    legacy = comp.AddTool('ofx.com.saog.resolve.dlss5')
    legacy.LoadSettings(str(Path(__file__).resolve().parents[2] / 'tests/LegacyHistory.setting'))
    assert legacy.GetInput('nrHistoryStartFrame', 0) == 12
    assert legacy.GetInput('nrHistoryStartMode', 0) == 1
    legacy_settings = args.output.parent / 'legacy-history-roundtrip.setting'
    legacy.SaveSettings(str(legacy_settings.resolve()))
    legacy_restored = comp.AddTool('ofx.com.saog.resolve.dlss5')
    legacy_restored.LoadSettings(str(legacy_settings.resolve()))
    assert legacy_restored.GetInput('nrHistoryStartFrame', 0) == 12
    for name in ('nrGuidanceMode', 'nrDepthConvention', 'nrMotionScaleX', 'nrMotionScaleY',
                 'nrHistoryStartMode', 'nrHistoryStartFrame'):
        assert controls[name]['INPB_Disabled'], f'Unavailable guide control must be disabled: {name}'
    # Verify a real OFX/Fusion settings round trip while later passes are hidden.
    tool.SetInput('nrPassCount', 0, 0)
    settings_path = args.output.parent / 'ofx-parameter-roundtrip.setting'
    settings_path.parent.mkdir(parents=True, exist_ok=True)
    tool.SaveSettings(str(settings_path.resolve()))
    assert settings_path.is_file(), 'Parameter settings were not saved'
    restored = comp.AddTool('ofx.com.saog.resolve.dlss5')
    assert restored is not None
    restored.LoadSettings(str(settings_path.resolve()))
    assert restored.GetInput('nrPassCount', 0) == 0
    assert restored.GetInput('nrPass2Intensity', 0) == 1.25
    assert restored.GetInput('nrPass3Intensity', 0) == 0.75
    assert restored.GetInput('nrShowAdvanced', 0) == 1
    assert abs(restored.GetInput('nrHueProtection', 0) - 0.65) < 1e-6
    assert restored.GetInput('nrInputEncoding', 0) == 5
    assert restored.GetInput('nrOpticalFlowMethod',0)==2
    assert restored.GetInput('nrNvidiaFlowQuality',0)==4
    assert restored.GetInput('nrExternalUnits',0)==1
    assert restored.GetInput('nrExternalYUp',0)==1
    assert restored.GetInput('nrExternalScaleX',0)==2
    assert restored.GetInput('nrHistoryStartFrame', 0) == tool.GetInput('nrHistoryStartFrame', 0)
    result = {
        'Host': resolve.GetProductName(), 'Version': resolve.GetVersionString(),
        'ContextCreated': True, 'InstanceCreated': True,
        'ControlCount': len(controls), 'GroupCount': len(group_ids),
        'InactivePassValuesPreserved': True,
        'AdvancedHdrHistoryValuesPreserved': True,
        'UnavailableGuidanceDisabled': True,
        'LegacyFullReplayControlsDisabled': True,
        'LegacyGroupsRemoved': True, 'ExternalMotionConnectorPresent': True,
        'ExternalMotionLinkAccepted': True,
        'OpticalFlowSettingsRoundTripPassed': True,
        'SettingsRoundTripPassed': True,
        'ProjectSavedOrEdited': False, 'FrameRendered': False,
        'NativeInspectorVisuallyVerified': False,
        'Controls': {name: {key: a[key] for key in ('INPS_Name', 'INPID_InputControl', 'INPN_Default', 'INPN_MinAllowed', 'INPN_MaxAllowed') if key in a} for name, a in controls.items()},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({key: value for key, value in result.items() if key != 'Controls'}, ensure_ascii=False, indent=2))
finally:
    comp.Unlock()
    comp.Close()
