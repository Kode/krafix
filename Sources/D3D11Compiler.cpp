#include "../glslang/glslang/Public/ShaderLang.h"
#include <map>
#include <string>

#ifdef SYS_WINDOWS
#define INITGUID
#include <Windows.h>
#include <d3d11.h>
#include <D3Dcompiler.h>
#include <fstream>
#include <iostream>
#endif

namespace {
	const char* shaderString(EShLanguage stage, int version) {
		if (version == 4) {
			switch (stage) {
			case EShLangVertex:
				return "vs_4_0";
			case EShLangFragment:
				return "ps_4_0";
			case EShLangGeometry:
				return "gs_4_0";
			case EShLangTessControl:
			case EShLangTessEvaluation:
			case EShLangCompute:
				return "unsupported";
			}
		}
		if (version == 4) {
			switch (stage) {
			case EShLangVertex:
				return "vs_5_0";
			case EShLangFragment:
				return "ps_5_0";
			case EShLangGeometry:
				return "gs_5_0";
			case EShLangTessControl:
			case EShLangTessEvaluation:
			case EShLangCompute:
				return "unsupported";
			}
		}
		return "unsupported";
	}
}

int compileHLSLToD3D11(const char* from, const char* to, const std::map<std::string, int>& attributes, EShLanguage stage) {
#ifdef SYS_WINDOWS
	FILE* in = fopen(from, "rb");
	if (!in) {
		printf("Error: unable to open input file: %s\n", from);
		return 1;
	}

	fseek(in, 0, SEEK_END);
	int length = ftell(in);
	rewind(in);

	char* data = new char[length];
	fread(data, 1, length, in);

	fclose(in);

	ID3DBlob* errorMessage;
	ID3DBlob* shaderBuffer;
	HRESULT hr = D3DCompile(data, length, from, nullptr, nullptr, "main", shaderString(stage, 4), /*D3DCOMPILE_DEBUG |*/ D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &shaderBuffer, &errorMessage);
	if (hr != S_OK) hr = D3DCompile(data, length, from, nullptr, nullptr, "main", shaderString(stage, 5), /*D3DCOMPILE_DEBUG |*/ D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, 0, &shaderBuffer, &errorMessage);
	if (hr == S_OK) {
		std::ofstream file(to, std::ios_base::binary);

		file.put((char)attributes.size());
		for (std::map<std::string, int>::const_iterator attribute = attributes.begin(); attribute != attributes.end(); ++attribute) {
			file << attribute->first.c_str();
			file.put(0);
			file.put(attribute->second);
		}

		ID3D11ShaderReflection* reflector = nullptr;
		D3DReflect(shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector);

		D3D11_SHADER_DESC desc;
		reflector->GetDesc(&desc);

		file.put(desc.BoundResources);
		for (unsigned i = 0; i < desc.BoundResources; ++i) {
			D3D11_SHADER_INPUT_BIND_DESC bindDesc;
			reflector->GetResourceBindingDesc(i, &bindDesc);
			if (strcmp(bindDesc.Name, "dx_ViewAdjust") != 0) file << "_"; // TODO: Remove when kfx is deprecated
			file << bindDesc.Name;
			file.put(0);
			file.put(bindDesc.BindPoint);
		}

		ID3D11ShaderReflectionConstantBuffer* constants = reflector->GetConstantBufferByName("$Globals");
		D3D11_SHADER_BUFFER_DESC bufferDesc;
		hr = constants->GetDesc(&bufferDesc);
		if (hr == S_OK) {
			file.put(bufferDesc.Variables);
			for (unsigned i = 0; i < bufferDesc.Variables; ++i) {
				ID3D11ShaderReflectionVariable* variable = constants->GetVariableByIndex(i);
				D3D11_SHADER_VARIABLE_DESC variableDesc;
				hr = variable->GetDesc(&variableDesc);
				if (hr == S_OK) {
					if (strcmp(variableDesc.Name, "dx_ViewAdjust") != 0) file << "_"; // TODO: Remove when kfx is deprecated
					file << variableDesc.Name;
					file.put(0);
					//file.put(variable->GetType()->);
					file.put(variableDesc.StartOffset);
					file.put(variableDesc.Size);
				}
			}
		}
		else {
			file.put(0);
		}
		file.write((char*)shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize());
		return 0;
	}
	else {
		std::cerr.write((char*)errorMessage->GetBufferPointer(), errorMessage->GetBufferSize());
		return 1;
	}
#else
	return 1;
#endif
}
