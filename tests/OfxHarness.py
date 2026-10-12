"""Isolated OFX test host; loads a candidate without installing or opening Resolve."""
import argparse
import ctypes as C
import json
import hashlib
import math
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--plugin', required=True, type=Path)
p.add_argument('--sdk', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
args = p.parse_args()
P, I, D, S = C.c_void_p, C.c_int, C.c_double, C.c_char_p
PP = C.POINTER(P)
objects, callbacks, suites, buffers, fetches, messages, missing = {}, [], {}, [], [], [], []
params, clips, values = {}, {}, {}

def handle(initial=None):
    value = len(objects) + 1
    objects[value] = initial or {}
    return value

def put(h, key, value):
    objects[h][key] = list(value) if isinstance(value, (list, tuple)) else [value]

def cb(types, fn, result=I):
    def guarded(*a):
        try:
            return fn(*a)
        except Exception as e:
            messages.append(f'callback error {fn.__name__}: {e}')
            return 1
    value = C.CFUNCTYPE(result, *types)(guarded)
    callbacks.append(value)
    return C.cast(value, P).value

def suite(name, functions):
    fields = re.findall(r'\(\*(\w+)\)', (args.sdk / (name + '.h')).read_text())
    cls = type(name + 'Suite', (C.Structure,), {'_fields_': [(f, P) for f in fields]})
    out = cls(**{f: functions.get(f, cb([], lambda: 5)) for f in fields})
    buffers.append(out)
    return C.addressof(out)

property_functions = {}
for label, typ in [('Pointer', P), ('String', S), ('Double', D), ('Int', I)]:
    def setter(h, key, index, value):
        array = objects[h].setdefault(key.decode(), [])
        while len(array) <= index:
            array.append(None)
        array[index] = value
        return 0
    def getter(h, key, index, out, typ=typ):
        array = objects[h].get(key.decode(), [])
        if not array:
            missing.append(key.decode())
        value = array[index] if index < len(array) else None
        out[0] = value if value is not None else (b'' if typ is S else 0)
        return 0
    def set_many(h, key, count, value, setter=setter):
        for i in range(count):
            setter(h, key, i, value[i])
        return 0
    def get_many(h, key, count, out, getter=getter, typ=typ):
        for i in range(count):
            getter(h, key, i, C.cast(C.byref(out.contents, i * C.sizeof(typ)), C.POINTER(typ)))
        return 0
    property_functions['propSet' + label] = cb([P, S, I, typ], setter)
    property_functions['propGet' + label] = cb([P, S, I, C.POINTER(typ)], getter)
    property_functions['propSet' + label + 'N'] = cb([P, S, I, C.POINTER(typ)], set_many)
    property_functions['propGet' + label + 'N'] = cb([P, S, I, C.POINTER(typ)], get_many)
def dimension(h, key, out):
    out[0] = len(objects[h].get(key.decode(), []))
    return 0
property_functions['propReset'] = cb([P, S], lambda h, key: (objects[h].pop(key.decode(), None), 0)[1])
property_functions['propGetDimension'] = cb([P, S, C.POINTER(I)], dimension)
suites[b'OfxPropertySuite', 1] = suite('ofxProperty', property_functions)

def same_handle(h, out):
    out[0] = h
    return 0
def define_param(h, kind, name, out):
    name = name.decode()
    value = handle({'OfxParamPropType': [kind], 'OfxPropName': [name.encode()]})
    params[name] = value
    out[0] = value
    return 0
def get_param(h, name, out, props):
    value = params[name.decode()]
    out[0] = props[0] = value
    return 0
def param_value(h, out):
    kind = objects[h]['OfxParamPropType'][0]
    value = values.get(h, objects[h].get('OfxParamPropDefault', [0])[0])
    typ = D if kind == b'OfxParamTypeDouble' else S if kind == b'OfxParamTypeString' else I
    C.cast(out, C.POINTER(typ))[0] = value
    return 0
def param_set_string(h, value):
    values[h] = value
    return 0
suites[b'OfxParameterSuite', 1] = suite('ofxParam', {
    'paramDefine': cb([P, S, S, PP], define_param),
    'paramGetHandle': cb([P, S, PP, PP], get_param),
    'paramSetGetPropertySet': cb([P, PP], same_handle),
    'paramGetPropertySet': cb([P, PP], same_handle),
    'paramGetValue': cb([P, P], param_value),
    'paramGetValueAtTime': cb([P, D, P], lambda h, time, out: param_value(h, out)),
    'paramSetValue': cb([P, S], param_set_string),
})
w, h = 192, 128
source = (C.c_float * (w*h*4))()
destination = (C.c_float * (w*h*4))()
external = (C.c_float * (w*h*4))()
for y in range(h):
    for x in range(w):
        i = (y*w+x)*4
        for c in range(3):
            source[i+c] = .12+.65*((x*17+y*31+c*7)%83)/83
        source[i+3] = .75
buffers += [source, destination, external]
def define_clip(effect, name, out):
    name = name.decode()
    value = handle()
    for key, data in {'OfxPropName': name.encode(), 'OfxImageEffectPropPixelDepth': b'OfxBitDepthFloat',
        'OfxImageEffectPropComponents': b'OfxImageComponentRGBA', 'OfxImageEffectPropPreMultiplication': b'OfxImageAlphaUnPremultiplied',
        'OfxImageEffectPropFrameRate': 24., 'OfxImageEffectPropFrameRange': [0., 10000.],
        'OfxImagePropPixelAspectRatio': 1., 'OfxImageClipPropConnected': int(name != 'MotionVectors')}.items():
        put(value, key, data)
    clips[name] = value
    out[0] = value
    return 0
def get_clip(effect, name, out, props):
    out[0] = props[0] = clips[name.decode()]
    return 0
def image(clip, time, region, out):
    name = objects[clip]['OfxPropName'][0].decode()
    fetches.append([name, time])
    data = destination if name == 'Output' else external if name == 'MotionVectors' else source
    value = handle()
    for key, item in {'OfxPropType': b'OfxTypeImage', 'OfxImageEffectPropPixelDepth': b'OfxBitDepthFloat',
        'OfxImageEffectPropComponents': b'OfxImageComponentRGBA', 'OfxImageEffectPropPreMultiplication': b'OfxImageAlphaUnPremultiplied',
        'OfxImagePropBounds': [0, 0, w, h], 'OfxImagePropRegionOfDefinition': [0, 0, w, h],
        'OfxImagePropData': C.addressof(data), 'OfxImagePropRowBytes': w*16,
        'OfxImagePropPixelAspectRatio': 1., 'OfxImagePropField': b'OfxFieldNone',
        'OfxImageEffectPropRenderScale': [1., 1.], 'OfxImagePropUniqueIdentifier': f'{name}-{time}'.encode()}.items():
        put(value, key, item)
    out[0] = value
    return 0
suites[b'OfxImageEffectSuite', 1] = suite('ofxImageEffect', {
    'getPropertySet': cb([P, PP], same_handle), 'getParamSet': cb([P, PP], same_handle),
    'clipDefine': cb([P, S, PP], define_clip), 'clipGetHandle': cb([P, S, PP, PP], get_clip),
    'clipGetPropertySet': cb([P, PP], same_handle), 'clipGetImage': cb([P, D, P, PP], image),
    'clipReleaseImage': cb([P], lambda h: 0), 'abort': cb([P], lambda h: 0),
})
def allocate(owner, size, out):
    data = C.create_string_buffer(size)
    buffers.append(data)
    out[0] = C.addressof(data)
    return 0
suites[b'OfxMemorySuite', 1] = suite('ofxMemory', {'memoryAlloc': cb([P, C.c_size_t, PP], allocate), 'memoryFree': cb([P], lambda h: 0)})
def cpu_count(out):
    out[0] = 1
    return 0
suites[b'OfxMultiThreadSuite', 1] = suite('ofxMultiThread', {'multiThreadNumCPUs': cb([C.POINTER(C.c_uint)], cpu_count)})
msg = cb([P, S, S, S], lambda h, typ, name, text: (messages.append(text.decode()), 0)[1])
message_v1 = (P*1)(msg)
message_v2 = (P*3)(msg, msg, cb([P], lambda h: 0))
buffers += [message_v1, message_v2]
suites[b'OfxMessageSuite', 1] = C.addressof(message_v1)
suites[b'OfxMessageSuite', 2] = C.addressof(message_v2)
host_props = handle()
for key, item in {'OfxPropType': b'OfxTypeImageEffectHost', 'OfxPropName': b'com.saog.test.OfxHarness',
    'OfxPropLabel': b'Isolated test host', 'OfxImageEffectPropSupportedComponents': [b'OfxImageComponentRGBA'],
    'OfxImageEffectPropSupportedPixelDepths': [b'OfxBitDepthFloat'],
    'OfxImageEffectPropSupportedContexts': [b'OfxImageEffectContextFilter', b'OfxImageEffectContextGeneral'],
    'OfxImageEffectPropTemporalClipAccess': 1, 'OfxParamHostPropMaxParameters': 1000,
    'OfxParamHostPropMaxPages': 20, 'OfxParamHostPropPageRowColumnCount': [30, 2]}.items():
    put(host_props, key, item)
class Host(C.Structure):
    _fields_ = [('host', P), ('fetchSuite', P)]
host = Host(host_props, cb([P, S, I], lambda h, name, version: suites.get((name, version), 0), P))
class Plugin(C.Structure):
    _fields_ = [('api', S), ('apiVersion', I), ('identifier', S), ('major', C.c_uint), ('minor', C.c_uint), ('setHost', P), ('main', P)]
plugin_hash = hashlib.sha256(args.plugin.read_bytes()).hexdigest().upper()
dll = C.CDLL(str(args.plugin.resolve()))
dll.OfxGetPlugin.argtypes = [I]
dll.OfxGetPlugin.restype = C.POINTER(Plugin)
plugin = dll.OfxGetPlugin(0).contents
C.CFUNCTYPE(None, C.POINTER(Host))(plugin.setHost)(C.byref(host))
entry = C.CFUNCTYPE(I, S, P, P, P)(plugin.main)
def action(name, effect=None, inputs=None, outputs=None):
    status = entry(name.encode(), effect, inputs, outputs)
    assert status in (0, 14), (name, status, messages[-3:], fetches[-3:], sorted(set(missing)))
    return status
effect = handle()
put(effect, 'OfxPropType', b'OfxTypeImageEffect')
put(effect, 'OfxImageEffectPropContext', b'OfxImageEffectContextFilter')
action('OfxActionLoad')
action('OfxActionDescribe', effect)
describe = handle({'OfxImageEffectPropContext': [b'OfxImageEffectContextFilter']})
action('OfxImageEffectActionDescribeInContext', effect, describe)
af = objects[params['nrAntiFlickerMode']]
assert af['OfxParamPropChoiceOption'] == [b'Off', b'Static', b'Flow', b'Flow+', b'Low Frequency']
assert af['OfxParamPropDefault'] == [0]
action('OfxActionCreateInstance', effect)
def change(name, value):
    values[params[name]] = value
    props = handle({'OfxPropName': [name.encode()], 'OfxPropType': [b'OfxTypeParameter'],
        'OfxPropChangeReason': [b'OfxChangeUserEdited'], 'OfxPropTime': [0.], 'OfxImageEffectPropRenderScale': [1., 1.]})
    action('OfxActionInstanceChanged', effect, props)
change('nrOpticalFlowMethod', 0)
change('nrAntiFlickerMode', 2)
assert objects[params['nrTemporalFlowMethod']]['OfxParamPropSecret'] == [0]
assert objects[params['nrNvidiaFlowQuality']]['OfxParamPropSecret'] == [0]
def needed(time):
    out = handle()
    action('OfxImageEffectActionGetFramesNeeded', effect, handle({'OfxPropTime': [time]}), out)
    return objects[out]
af_needed = needed(10.)
assert any(v == [9., 10.] for v in af_needed.values()), af_needed
change('nrAntiFlickerMode', 1)
static_needed = needed(10.)
assert any(v == [10., 10.] for v in static_needed.values()), static_needed
def render(time):
    props = handle({'OfxPropTime': [time], 'OfxImageEffectPropRenderWindow': [0, 0, w, h],
        'OfxImageEffectPropRenderScale': [1., 1.], 'OfxImageEffectPropFieldToRender': [b'OfxFieldNone'],
        'OfxImageEffectPropInteractiveRenderStatus': [1], 'OfxImageEffectPropSequentialRenderStatus': [1]})
    action('OfxImageEffectActionRender', effect, props)
    out = list(destination)
    assert all(math.isfinite(v) for v in out)
    assert out[3::4] == list(source)[3::4]
    return out
outputs = []
for mode in range(5):
    change('nrAntiFlickerMode', mode)
    # External zero vectors provide a precise static correspondence while
    # exercising the Fusion connector and all temporal modes in the real OFX.
    change('nrExternalMotion', int(mode >= 2))
    put(clips['MotionVectors'], 'OfxImageClipPropConnected', int(mode >= 2))
    first, second = render(10.), render(11.)
    assert render(11.) == second, 'Repeated OFX render changed output'
    outputs.append({'Mode': mode, 'ChangedPixels': sum(a != b for a, b in zip(second, source))})
change('nrExternalMotion', 0)
change('nrAntiFlickerMode', 0)
render(20.)
warm = render(21.)
change('nrAntiFlickerMode', 1)
assert max(abs(a-b) for a, b in zip(render(21.), warm)) < 2e-6, 'Post-only AF change reran/reset NR'
change('nrAntiFlickerResponse', 120.)
assert max(abs(a-b) for a, b in zip(render(21.), warm)) < 2e-6, 'Response change reran/reset NR'
# NR None + internally estimated AF motion exercises the independent demand.
change('nrAntiFlickerMode', 2)
render(22.)
assert ['Source', 21.] in fetches, 'AF-only internal flow did not fetch its single reference'
assert objects[params['nrTemporalFlowMethod']]['OfxParamPropSecret'] == [0]
change('nrTemporalFlowMethod', 0)
assert objects[params['nrAmdFlowQuality']]['OfxParamPropSecret'] == [0]
render(22.)
change('nrTemporalFlowMethod', 1)
codec_outputs = []
for encoding in (1, 4, 3, 5, 2, 6):
    change('nrInputEncoding', encoding)
    for i in range(w*h):
        for c in range(3):
            source[i*4+c] = (-.015 if c == 1 and i % 11 == 0 else 2.+(i % 71)/30) if encoding in (2, 6) else .12+.65*((i*17+c*7)%83)/83
    change('nrExternalMotion', 1)
    put(clips['MotionVectors'], 'OfxImageClipPropConnected', 1)
    for mode in (1, 2, 3, 4):
        change('nrAntiFlickerMode', mode)
        render(30.)
        value = render(31.)
        if encoding in (2, 6):
            assert max(value[0::4]) > 1, 'HDR superwhite was clipped'
        # The model may legitimately change a negative input to positive. Check
        # the temporal stage against its actual unfiltered NR result instead of
        # prescribing the model's output sign.
        change('nrAntiFlickerMode', 0)
        raw_nr = render(31.)
        change('nrAntiFlickerMode', mode)
        cold = render(31.)
        if mode != 3:
            assert max(abs(a-b) for a, b in zip(cold, raw_nr)) < 2e-5, 'Cold AF changed the current NR correction'
        codec_outputs.append({'Encoding': encoding, 'Mode': mode, 'FiniteAndAlphaPreserved': True,
            'RawNrMinGreen': min(raw_nr[1::4]), 'TemporalMinGreen': min(value[1::4])})
change('nrResidualStrength', 0.)
assert render(11.) == list(source), 'Strength zero retains old correction'
action('OfxActionDestroyInstance', effect)
action('OfxActionUnload')
assert hashlib.sha256(args.plugin.read_bytes()).hexdigest().upper() == plugin_hash, 'Candidate changed during verification'
report = {'Host': 'Isolated Python OFX suite harness', 'Plugin': str(args.plugin.resolve()), 'PluginSHA256': plugin_hash,
    'DescriptorAndInstancePassed': True, 'FiveChoicesDefaultOff': True,
    'AfOnlyFlowQualityVisibilityPassed': True, 'FrameDependenciesBoundedToOnePrevious': True,
    'ActualCandidateOfxRenderPassed': True, 'DuplicateExact': True, 'ZeroStrengthExact': True,
    'PostOnlyAfChangesPreserveNrOutput': True, 'InternalFlowWithNrNonePassed': True,
    'AfOnlyAmdAndNvidiaFlowPassed': True,
    'SixEncodingsFourModesRealNrPassed': True, 'CodecOutputs': codec_outputs,
    'Outputs': outputs, 'Fetches': fetches, 'Messages': messages,
    'ResolveLaunched': False, 'NativeResolveInspectorOrExportTested': False}
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
evidence = {'PluginSHA256': plugin_hash,
    'ParameterReportFile': args.output.name,
    'ParameterReportSHA256': hashlib.sha256(args.output.read_bytes()).hexdigest().upper(),
    'ParameterProofKind': 'IndependentOFXSuiteHost', 'StandaloneDescriptorAndRenderPassed': True,
    'NativeParameterRoundTripPassed': False, 'NativePreviewRenderObserved': False,
    'NativeTimelineNRExportAccepted': False}
(args.output.parent / 'host-evidence.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
print(json.dumps({k: v for k, v in report.items() if k not in ('Fetches', 'Messages', 'CodecOutputs')}, indent=2))
