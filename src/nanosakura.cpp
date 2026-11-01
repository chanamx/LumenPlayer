#include "../third_party/onnxruntime/include/onnxruntime_c_api.h"
#include "nanosakura.h"
#include "caption_tokenizer.h"
#include <cstring>
#include <stdexcept>
#include <array>

struct NanoSakura::Impl {
    HMODULE dll=nullptr;
    const OrtApi* api=nullptr;
    OrtEnv* env=nullptr;
    OrtMemoryInfo* memory=nullptr;
    OrtRunOptions* run=nullptr;
    OrtSession* encoder=nullptr,*cross=nullptr,*decoder=nullptr;
    std::unique_ptr<NanoSakuraTokenizer> tokenizer;
    std::mutex runMutex;
    void Check(OrtStatus* status){if(!status)return;std::string message=api->GetErrorMessage(status);api->ReleaseStatus(status);throw std::runtime_error(message);}
    struct Tensor {
        const OrtApi* api;OrtValue* value=nullptr;
        explicit Tensor(const OrtApi* a):api(a){}
        ~Tensor(){if(value)api->ReleaseValue(value);}
        Tensor(const Tensor&)=delete;Tensor& operator=(const Tensor&)=delete;
        void Clear(){if(value)api->ReleaseValue(value);value=nullptr;}
    };
    void TensorData(Tensor& tensor,void* data,size_t bytes,std::initializer_list<int64_t> shape,ONNXTensorElementDataType type){
        Check(api->CreateTensorWithDataAsOrtValue(memory,data,bytes,shape.begin(),shape.size(),type,&tensor.value));
    }
    void ZeroTensor(Tensor& tensor,std::initializer_list<int64_t> shape){
        OrtAllocator* allocator=nullptr;Check(api->GetAllocatorWithDefaultOptions(&allocator));
        Check(api->CreateTensorAsOrtValue(allocator,shape.begin(),shape.size(),ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&tensor.value));
        void* data=nullptr;Check(api->GetTensorMutableData(tensor.value,&data));
        size_t elements=1;for(auto dimension:shape)elements*=size_t(dimension);
        std::memset(data,0,elements*sizeof(float));
    }
    ~Impl(){
        if(api){if(decoder)api->ReleaseSession(decoder);if(cross)api->ReleaseSession(cross);if(encoder)api->ReleaseSession(encoder);
            if(run)api->ReleaseRunOptions(run);if(memory)api->ReleaseMemoryInfo(memory);if(env)api->ReleaseEnv(env);}
        if(dll)FreeLibrary(dll);
    }
};

NanoSakura::NanoSakura():impl_(new Impl){}
NanoSakura::~NanoSakura()=default;
void NanoSakura::Load(const std::wstring& directory,const std::atomic<bool>& cancelled){
    auto& im=*impl_;
    im.dll=LoadLibraryExW(PathJoin(ExeDir(),L"onnxruntime.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!im.dll)throw std::runtime_error("Cannot load installed ONNX runtime");
    const auto symbol=GetProcAddress(im.dll,"OrtGetApiBase");
    decltype(&OrtGetApiBase) get=nullptr;static_assert(sizeof(get)==sizeof(symbol));std::memcpy(&get,&symbol,sizeof(get));
    if(!get)throw std::runtime_error("Missing ONNX runtime entry");
    const auto* base=get();im.api=base->GetApi(ORT_API_VERSION);
    if(!im.api||std::string(base->GetVersionString())!="1.28.2")throw std::runtime_error("Unexpected ONNX runtime version");
    if(cancelled.load())return;
    im.tokenizer=std::make_unique<NanoSakuraTokenizer>(PathJoin(directory,L"tokenizer.json"));
    im.Check(im.api->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"lumen-nanosakura",&im.env));
    im.Check(im.api->CreateCpuMemoryInfo(OrtDeviceAllocator,OrtMemTypeDefault,&im.memory));
    {std::lock_guard<std::mutex> lock(im.runMutex);im.Check(im.api->CreateRunOptions(&im.run));}
    OrtSessionOptions* raw=nullptr;im.Check(im.api->CreateSessionOptions(&raw));
    std::unique_ptr<OrtSessionOptions,decltype(im.api->ReleaseSessionOptions)> options(raw,im.api->ReleaseSessionOptions);
    im.Check(im.api->SetIntraOpNumThreads(raw,1));im.Check(im.api->SetInterOpNumThreads(raw,1));
    im.Check(im.api->SetSessionExecutionMode(raw,ORT_SEQUENTIAL));im.Check(im.api->DisableCpuMemArena(raw));
    im.Check(im.api->AddSessionConfigEntry(raw,"session.intra_op.allow_spinning","0"));
    im.Check(im.api->AddSessionConfigEntry(raw,"session.inter_op.allow_spinning","0"));
    for(const auto& session:std::array<std::pair<const wchar_t*,OrtSession**>,3>{{{L"i8/encoder.onnx",&im.encoder},{L"i8/cross_kv.onnx",&im.cross},{L"i8/decoder_step.onnx",&im.decoder}}}){
        if(cancelled.load())return;
        im.Check(im.api->CreateSession(im.env,PathJoin(directory,session.first).c_str(),raw,session.second));
    }
}
void NanoSakura::Cancel(){auto& im=*impl_;std::lock_guard<std::mutex> lock(im.runMutex);if(im.run){auto* status=im.api->RunOptionsSetTerminate(im.run);if(status)im.api->ReleaseStatus(status);}}
std::wstring NanoSakura::Translate(const std::wstring& text,const std::function<bool()>& obsolete){
    auto& im=*impl_;if(obsolete())return {};
    auto ids=im.tokenizer->Encode(text+L"<eos>");if(ids.empty()||ids.size()>256)return {};
    const int64_t length=(int64_t)ids.size();std::vector<int64_t> mask(ids.size(),1);
    Impl::Tensor input(im.api),attention(im.api),hidden(im.api),keys(im.api),values(im.api);
    im.TensorData(input,ids.data(),ids.size()*sizeof(int64_t),{1,length},ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    im.TensorData(attention,mask.data(),mask.size()*sizeof(int64_t),{1,length},ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    const char* encNames[]={"input_ids","attention_mask"};const OrtValue* encInputs[]={input.value,attention.value};
    const char* encOut[]={"encoder_hidden_states"};
    im.Check(im.api->Run(im.encoder,im.run,encNames,encInputs,2,encOut,1,&hidden.value));
    if(obsolete())return {};
    const char* crossNames[]={"encoder_hidden_states"};const OrtValue* crossInputs[]={hidden.value};const char* crossOut[]={"all_k_enc","all_v_enc"};
    OrtValue* kv[]={nullptr,nullptr};
    // Assign both output handles even on error so their owners always release them.
    auto* status=im.api->Run(im.cross,im.run,crossNames,crossInputs,1,crossOut,2,kv);keys.value=kv[0];values.value=kv[1];im.Check(status);
    hidden.Clear();
    std::vector<uint8_t> boolMask(ids.size(),1);int64_t current=1;
    Impl::Tensor token(im.api),crossMask(im.api),conv(im.api),state(im.api);
    im.TensorData(token,&current,sizeof(current),{1,1},ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64);
    im.TensorData(crossMask,boolMask.data(),boolMask.size(),{1,1,1,length},ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL);
    // ORT owns these buffers, so the first step can release them with its old
    // input values instead of retaining another 9.25 MiB for the entire cue.
    im.ZeroTensor(conv,{12,1,3,1792});
    im.ZeroTensor(state,{12,1,24,128,64});
    const char* decNames[]={"dec_token_id","cross_mask","all_k_enc","all_v_enc","all_conv_states","all_ssm_states"};
    const char* decOut[]={"logits","new_conv_states","new_ssm_states"};
    std::vector<int64_t> generated;const double deadline=QpcNow()+12;
    for(int step=0;step<128;++step){
        if(obsolete()||QpcNow()>deadline)return {};
        const OrtValue* inputs[]={token.value,crossMask.value,keys.value,values.value,conv.value,state.value};
        Impl::Tensor logits(im.api),nextConv(im.api),nextState(im.api);OrtValue* outputs[]={nullptr,nullptr,nullptr};
        status=im.api->Run(im.decoder,im.run,decNames,inputs,6,decOut,3,outputs);
        logits.value=outputs[0];nextConv.value=outputs[1];nextState.value=outputs[2];im.Check(status);
        OrtTensorTypeAndShapeInfo* rawShape=nullptr;im.Check(im.api->GetTensorTypeAndShape(logits.value,&rawShape));
        std::unique_ptr<OrtTensorTypeAndShapeInfo,decltype(im.api->ReleaseTensorTypeAndShapeInfo)> shape(rawShape,im.api->ReleaseTensorTypeAndShapeInfo);
        size_t elements=0;im.Check(im.api->GetTensorShapeElementCount(rawShape,&elements));
        if(elements!=65536)throw std::runtime_error("Unexpected NanoSakura logits shape");
        float* data=nullptr;im.Check(im.api->GetTensorMutableData(logits.value,(void**)&data));
        current=std::max_element(data,data+65536)-data;
        if(current==2)return obsolete()?std::wstring():im.tokenizer->Decode(generated);
        generated.push_back(current);
        std::swap(conv.value,nextConv.value);std::swap(state.value,nextState.value);
    }
    return {}; // Do not publish an unfinished or truncated translation.
}
