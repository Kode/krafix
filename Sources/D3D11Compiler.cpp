#include "../glslang/glslang/Public/ShaderLang.h"
#include <map>
#include <string>

#ifdef _WIN32
#define INITGUID
#include <Windows.h>
#include <d3d11.h>
#include <D3Dcompiler.h>
#include <fstream>
#include <iostream>
#include <strstream>
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
				return "unsupported";
			case EShLangTessEvaluation:
				return "unsupported";
			case EShLangCompute:
				return "cs_4_0";
			}
		}
		else if (version == 5) {
			switch (stage) {
			case EShLangVertex:
				return "vs_5_0";
			case EShLangFragment:
				return "ps_5_0";
			case EShLangGeometry:
				return "gs_5_0";
			case EShLangTessControl:
				return "hs_5_0";
			case EShLangTessEvaluation:
				return "ds_5_0";
			case EShLangCompute:
				return "cs_5_0";
			}
		}
		return "unsupported";
	}
}

int compileHLSLToD3D11(const char* fromRelative, const char* to, const char* source, char* output, int* outputlength, const std::map<std::string, int>& attributes, EShLanguage stage, bool debug) {
#ifdef _WIN32
	char from[256];
	int length;
	char* data;
	if (source) {
		strcpy(from, fromRelative);
		data = (char*)source;
		length = strlen(source);
	}
	else {
		GetFullPathNameA(fromRelative, 255, from, nullptr);

		FILE* in = fopen(from, "rb");
		if (!in) {
			printf("Error: unable to open input file: %s\n", from);
			return 1;
		}

		fseek(in, 0, SEEK_END);
		length = ftell(in);
		rewind(in);

		data = new char[length];
		fread(data, 1, length, in);

		fclose(in);
	}

	ID3DBlob* errorMessage;
	ID3DBlob* shaderBuffer;
	UINT flags = 0;
	if (debug) flags |= D3DCOMPILE_DEBUG;
	HRESULT hr = D3DCompile(data, length, from, nullptr, nullptr, "main", shaderString(stage, 4), flags, 0, &shaderBuffer, &errorMessage);
	if (hr != S_OK) hr = D3DCompile(data, length, from, nullptr, nullptr, "main", shaderString(stage, 5), flags, 0, &shaderBuffer, &errorMessage);
	if (hr == S_OK) {
		std::ostream* file;
		std::ofstream actualfile;
		std::ostrstream arrayout(output, 1024 * 1024);
		*outputlength = 0;

		if (output) {
			file = &arrayout;
		}
		else {
			actualfile.open(to, std::ios_base::binary);
			file = &actualfile;
		}

		file->put((char)attributes.size()); *outputlength += 1;
		for (std::map<std::string, int>::const_iterator attribute = attributes.begin(); attribute != attributes.end(); ++attribute) {
			(*file) << attribute->first.c_str(); *outputlength += attribute->first.length();
			file->put(0); *outputlength += 1;
			file->put(attribute->second); *outputlength += 1;
		}

		ID3D11ShaderReflection* reflector = nullptr;
		D3DReflect(shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)&reflector);

		D3D11_SHADER_DESC desc;
		reflector->GetDesc(&desc);

		file->put(desc.BoundResources); *outputlength += 1;
		for (unsigned i = 0; i < desc.BoundResources; ++i) {
			D3D11_SHADER_INPUT_BIND_DESC bindDesc;
			reflector->GetResourceBindingDesc(i, &bindDesc);
			(*file) << bindDesc.Name; *outputlength += strlen(bindDesc.Name);
			file->put(0); *outputlength += 1;
			file->put(bindDesc.BindPoint); *outputlength += 1;
		}

		ID3D11ShaderReflectionConstantBuffer* constants = reflector->GetConstantBufferByName("$Globals");
		D3D11_SHADER_BUFFER_DESC bufferDesc;
		hr = constants->GetDesc(&bufferDesc);
		if (hr == S_OK) {
			file->put(bufferDesc.Variables); *outputlength += 1;
			for (unsigned i = 0; i < bufferDesc.Variables; ++i) {
				ID3D11ShaderReflectionVariable* variable = constants->GetVariableByIndex(i);
				D3D11_SHADER_VARIABLE_DESC variableDesc;
				hr = variable->GetDesc(&variableDesc);
				if (hr == S_OK) {
					(*file) << variableDesc.Name; *outputlength += strlen(variableDesc.Name);
					file->put(0); *outputlength += 1;
					file->write((char*)&variableDesc.StartOffset, 4); *outputlength += 4;
					file->write((char*)&variableDesc.Size, 4); *outputlength += 4;
					D3D11_SHADER_TYPE_DESC typeDesc;
					hr = variable->GetType()->GetDesc(&typeDesc);
					if (hr == S_OK) {
						file->put(typeDesc.Columns); *outputlength += 1;
						file->put(typeDesc.Rows); *outputlength += 1;
					}
					else {
						file->put(0); *outputlength += 1;
						file->put(0); *outputlength += 1;
					}
				}
			}
		}
		else {
			file->put(0); *outputlength += 1;
		}
		file->write((char*)shaderBuffer->GetBufferPointer(), shaderBuffer->GetBufferSize()); *outputlength += shaderBuffer->GetBufferSize();
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
