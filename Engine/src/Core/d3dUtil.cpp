#include <Engine/Core/d3dUtil.h>
#include <comdef.h>
#include <fstream>
#include <filesystem>

using Microsoft::WRL::ComPtr;


bool d3dUtil::IsKeyDown(int vkeyCode)
{
    return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}


ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

    fin.read((char*)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;

    // Create the actual default buffer resource.
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap. 
    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


    // Describe the data we want to copy into the default buffer.
    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = initData;
    subResourceData.RowPitch = byteSize;
    subResourceData.SlicePitch = subResourceData.RowPitch;

    // Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
    // will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
    // the intermediate upload heap data will be copied to mBuffer.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(), 
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    // Note: uploadBuffer has to be kept alive after the above function calls because
    // the command list has not been executed yet that performs the actual copy.
    // The caller can Release the uploadBuffer after it knows the copy has been executed.


    return defaultBuffer;
}


/**
 * @brief Convert an HRESULT value into a human-readable message.
 * 
 * This function takes a Windows HRESULT code (which may represent either success or failure) and returns its descriptive text message.
 *
 * Internally it uses the helper class `_com_error` (defined in <comdef.h>),
 * which wraps the HRESULT and provides functions such as `ErrorMessage()`.
 *
 * @param hr - the HRESULT code returned by a Win32 or DirectX call.
 *           This can represent both error codes (e.g. E_FAIL, 0x80004005)
 *           or success codes (e.g. S_OK, 0x00000000).
 *
 * @return A wide string (`std::wstring`) containing the system-provided
 *         description of the code. For example:
 *         
           - S_OK -> "Operation successful."
 *
           - E_FAIL -> "Unspecified error"
 *
           - 0x80070002 -> "The system cannot find the file specified."
 *
 *         If the message cannot be resolved, an empty string is returned.
 */
static std::wstring HrToMessage(HRESULT hr)
{
    // _com_error is a helper class that can take an error (or success) code from HRESULT and convert it into a message
    _com_error err(hr);
    return err.ErrorMessage() ? err.ErrorMessage() : L"";
}


/**
 * @brief Convert a narrow (ANSI) byte string to a UTF-16 wide string.
 *
 * Performs a two-pass conversion using the Windows API function
 * MultiByteToWideChar. The source is interpreted using the system ANSI
 * code page (CP_ACP), not UTF-8.
 *
 * The first pass queries the required number of UTF-16 code units, the second
 * performs the conversion into a std::wstring buffer of the exact size.
 * 
 * The conversion uses CP_ACP (ANSI Code Page). If your input is UTF-8, prefer CP_UTF8 and set
 * MB_ERR_INVALID_CHARS to detect invalid sequences (see the robust variant below).
 *
 * @param str - pointer to the source byte sequence (ANSI). May be non-null-terminated.
 * @param len - number of bytes to convert from str. If zero, the result is an empty string.
 *
 * @return std::wstring containing the converted UTF-16 text. If str is null or len is 0,
 *         returns an empty string. If the conversion fails, behavior depends on flags (here:
 *         best-effort because flags=0).
 */
static std::wstring AnsiStringToWideString(const char* str, size_t len)
{
    if (!str || !len) return L"";

    // Get the number of UTF-16 code units the string will have after being converted to std::wstring
    int wideStringLen = MultiByteToWideChar(CP_ACP, 0, str, (int)len, nullptr, 0);

    std::wstring wideString(wideStringLen, L'\0');

    // Convert to std::wstring
    MultiByteToWideChar(CP_ACP, 0, str, (int)len, &wideString[0], wideStringLen);

    return wideString;
}


///**
// * @brief Replace all spaces in a wide string with non-breaking spaces (NBSP).
// *
// * Iterates through the input string and replaces each U+0020 (regular space)
// * with U+00A0 (non-breaking space). A non-breaking space looks like a normal
// * space but prevents automatic line breaking at that position in many UI
// * elements (e.g., MessageBox, rich text).
// * 
// * Useful when displaying file paths or identifiers in UI elements
// * that may otherwise insert line breaks at spaces.
// *
// * @param str - input wide string (std::wstring). The original string is not modified.
// *
// * @return A copy of the input string where all spaces have been replaced by NBSP.
// */
//static std::wstring MakeNoWrap(const std::wstring& str) {
//    std::wstring strCopy = str;
//
//    // Replace regular spaces with non-breaking spaces (NBSP)
//    // so that the system does not break the line at this space by default
//    for (auto& ch : strCopy) if (ch == L' ') ch = L'\u00A0';
//
//    return strCopy;
//}



ComPtr<ID3DBlob> d3dUtil::CompileShader(
	const std::wstring& filename,
	const D3D_SHADER_MACRO* defines,
	const std::string& entrypoint,
	const std::string& target)
{
	UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	HRESULT hr = S_OK;

    std::wstring prettyPathToShader = std::filesystem::weakly_canonical(filename).wstring();
	ComPtr<ID3DBlob> byteCode = nullptr;
	ComPtr<ID3DBlob> errors;
	hr = D3DCompileFromFile(prettyPathToShader.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

    std::wstring compilerLog;
    if (errors) {
        const char* ptr = (const char*)errors->GetBufferPointer();
        size_t len = errors->GetBufferSize();
        // ???? ??? ? ANSI; ???? ? UTF-8 — ??????????? CP_UTF8
        int need = MultiByteToWideChar(CP_ACP, 0, ptr, (int)len, nullptr, 0);
        compilerLog.resize(need);
        MultiByteToWideChar(CP_ACP, 0, ptr, (int)len, compilerLog.data(), need);
    }

    if (FAILED(hr)) {
        std::wstringstream det;
        prettyPathToShader = MakeNoWrap(prettyPathToShader);
        det << L"Path to the shader: " << prettyPathToShader << L"\n";
        if (!compilerLog.empty()) det << L"\n" << compilerLog;

        ThrowIfFailedMsg(hr, L"Compile HLSL", det.str());
    }

    //if (FAILED(hr))
    //{
    //    std::wstring hlslErr;
    //    if (errors) {
    //        const char* ptr = reinterpret_cast<const char*>(errors->GetBufferPointer());
    //        size_t len = errors->GetBufferSize();
    //        hlslErr = AnsiStringToWideString(ptr, len);
    //    }

    //    prettyPathToShader = MakeNoWrap(prettyPathToShader);

    //    std::wstringstream ss;
    //    ss << L"Shader compilation failed.\n\n"
    //        << L"File:   " << prettyPathToShader << L"\n";

    //    if (!hlslErr.empty())
    //        ss << L"\nHLSL error(s):\n" << hlslErr << L"\n";

    //    const std::wstring hrMsg = HrToMessage(hr);
    //    if (!hrMsg.empty())
    //        ss << L"\nHRESULT: " << hrMsg << L" (0x" << std::hex << hr << L")\n";

    //    const std::wstring msg = ss.str();

    //    // ??????????? ????
    //    MessageBoxW(
    //        /*hWnd*/ nullptr,
    //        msg.c_str(),
    //        L"Shader Compilation Error",
    //        MB_OK | MB_ICONERROR | MB_TASKMODAL
    //    );

    //    // ?????????? ?????????? ??????? ??? ???? ????????:
    //    throw std::runtime_error(std::string(msg.begin(), msg.end()));
    //}

    // ???????? ?????????? — ???? ?????????? ??? ?? ???-?? ????? ? warnings
    //if (errors != nullptr) OutputDebugStringA(reinterpret_cast<const char*>(errors->GetBufferPointer()));

	return byteCode;
}



//DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
//    ErrorCode(hr),
//    FunctionName(functionName),
//    Filename(filename),
//    LineNumber(lineNumber)
//{}
//
//
//
//std::wstring DxException::ToString() const
//{
//    // Get the string description of the error code.
//    _com_error err(ErrorCode);
//    std::wstring msg = err.ErrorMessage();
//
//    return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
//}