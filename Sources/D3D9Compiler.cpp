#ifdef SYS_WINDOWS

#include "../glslang/glslang/Public/ShaderLang.h"
#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <fstream>
#include <iostream>
#include <map>

int compileHLSLToD3D9(const char* from, const char* to, const std::map<std::string, int>& attributes, EShLanguage stage) {
	LPD3DXBUFFER errors;
	LPD3DXBUFFER shader;
	LPD3DXCONSTANTTABLE table;
	HRESULT hr = D3DXCompileShaderFromFileA(from, nullptr, nullptr, "main", stage == EShLangVertex ? "vs_2_0" : "ps_2_0", 0, &shader, &errors, &table);
	if (FAILED(hr)) hr = D3DXCompileShaderFromFileA(from, nullptr, nullptr, "main", stage == EShLangVertex ? "vs_3_0" : "ps_3_0", 0, &shader, &errors, &table);
	if (errors != nullptr) std::cerr << (char*)errors->GetBufferPointer();
	if (!FAILED(hr)) {
		std::ofstream file(to, std::ios_base::binary);

		file.put(attributes.size());
		for (std::map<std::string, int>::const_iterator attribute = attributes.begin(); attribute != attributes.end(); ++attribute) {
			file << attribute->first.c_str();
			file.put(0);
			file.put(attribute->second);
		}

		D3DXCONSTANTTABLE_DESC desc;
		table->GetDesc(&desc);
		file.put(desc.Constants);
		for (UINT i = 0; i < desc.Constants; ++i) {
			D3DXHANDLE handle = table->GetConstant(nullptr, i);
			D3DXCONSTANT_DESC descriptions[10];
			UINT count = 10;
			table->GetConstantDesc(handle, descriptions, &count);
			if (count > 1) std::cerr << "Error: Number of descriptors for one constant is greater than one." << std::endl;
			for (UINT i2 = 0; i2 < count; ++i2) {
				char regtype;
				switch (descriptions[i2].RegisterSet) {
				case D3DXRS_BOOL:
					regtype = 'b';
					break;
				case D3DXRS_INT4:
					regtype = 'i';
					break;
				case D3DXRS_FLOAT4:
					regtype = 'f';
					break;
				case D3DXRS_SAMPLER:
					regtype = 's';
					break;
				}
				//std::cout << descriptions[i2].Name << " " << regtype << descriptions[i2].RegisterIndex << " " << descriptions[i2].RegisterCount << std::endl;
				file << descriptions[i2].Name;
				file.put(0);
				file.put(regtype);
				file.put(descriptions[i2].RegisterIndex);
				file.put(descriptions[i2].RegisterCount);
			}
		}
		DWORD* data = (DWORD*)shader->GetBufferPointer();
		for (unsigned i = 0; i < shader->GetBufferSize() / 4; ++i) {
			if ((data[i] & 0xffff) == 0xfffe) { //comment token
				unsigned size = (data[i] >> 16) & 0xffff;
				i += size;
			}
			else file.write((char*)&data[i], 4);
		}
		//file.write((char*)shader->GetBufferPointer(), shader->GetBufferSize());
		return 0;
	}
	else {
		std::cerr.write((char*)errors->GetBufferPointer(), errors->GetBufferSize());
		return 1;
	}
}

#endif
