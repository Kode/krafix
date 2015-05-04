#include "MetalTranslator.h"
#include <fstream>
#include <sstream>

using namespace krafix;

typedef unsigned id;

namespace {
	std::string positionName = "position";
	
	std::string extractFilename(std::string path) {
		int i = (int)path.size() - 1;
		for (; i > 0; --i) {
			if (path[i] == '/' || path[i] == '\\') {
				++i;
				break;
			}
		}
		return path.substr(i, std::string::npos);
	}
	
	std::string replace(std::string str, char c1, char c2) {
		std::string ret = str;
		for (int i = 0; i < str.length(); ++i) {
			if (str[i] == c1) ret[i] = c2;
		}
		return ret;
	}
}

void MetalTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	name = extractFilename(filename);
	name = name.substr(0, name.find_last_of("."));
	name = replace(name, '-', '_');
	name = replace(name, '.', '_');
	
	out.open(filename, std::ios::binary | std::ios::out);
	
	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		outputInstruction(target, attributes, inst);
		if (outputting) out << "\n";
	}
	
	out.close();
}

void MetalTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;
	
	switch (inst.opcode) {
		case OpExecutionMode:
			break;
		case OpTypeArray: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "?[]";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0) {
					t.name = "float[]";
					t.length = 2;
					types[id] = t;
				}
				if (strcmp(subtype.name, "float3") == 0) {
					t.name = "float3[]";
					t.length = 2;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeVector: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "float?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 2) {
					t.name = "float2";
					t.length = 2;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
					t.name = "float3";
					t.length = 3;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					t.name = "float4";
					t.length = 4;
				}
			}
			types[id] = t;
			break;
		}
		case OpTypeMatrix: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "matrix_float4x?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float4") == 0 && inst.operands[2] == 4) {
					t.name = "matrix_float4x4";
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeSampler: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "sampler2D";
			types[id] = t;
			break;
		}
		case OpVariable: {
			unsigned id = inst.operands[1];
			Variable& v = variables[id];
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			v.declared = v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
			if (names.find(id) != names.end()) {
				if (v.storage == StorageClassInput) {
					if (stage == EShLangVertex) {
						Type type = types[v.type];
						if (strcmp(type.name, "float2") == 0 || strcmp(type.name, "float3") == 0 || strcmp(type.name, "float4") == 0) references[id] = type.name + std::string("(vertices[vid].") + names[id].name + ")";
						else references[id] = std::string("vertices[vid].") + names[id].name;
					}
					else {
						references[id] = std::string("input.") + names[id].name;
					}
				}
				else if (v.storage == StorageClassOutput) {
					if (stage == EShLangVertex) references[id] = std::string("output.") + names[id].name;
					else references[id] = "output";
				}
				else if (v.storage == StorageClassUniformConstant) {
					Type type = types[v.type];
					if (strcmp(type.name, "sampler2D") == 0) references[id] = names[id].name;
					else references[id] = std::string("uniforms.") + names[id].name;
				}
				else {
					references[id] = names[id].name;
				}
			}
			break;
		}
		case OpFunction: {
			output(out);
			out << "#include <metal_stdlib>\n";
			out << "#include <simd/simd.h>\n";
			out << "\n";
			out << "using namespace metal;\n";
			out << "\n";
			indent(out);
			out << "struct " << name << "_uniforms {\n";
			++indentation;
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;
				
				Type t = types[variable.type];
				Name n = names[id];
				
				if (variable.storage == StorageClassUniformConstant) {
					if (strcmp(t.name, "sampler2D") != 0) {
						indent(out);
						out << t.name << " " << n.name << ";\n";
					}
				}
			}
			--indentation;
			indent(out);
			out << "};\n\n";
			
			out << "struct " << name << "_in {\n";
			++indentation;
			int i = 0;
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;
				
				Type t = types[variable.type];
				Name n = names[id];
				
				if (variable.storage == StorageClassInput) {
					indent(out);
					if (stage == EShLangVertex) {
						out << "packed_" << t.name << " " << n.name << ";\n";
					}
					else {
						out << t.name << " " << n.name << ";\n";
					}
					++i;
				}
			}
			--indentation;
			indent(out);
			out << "};\n\n";
			
			if (stage == EShLangVertex) {
				indent(out);
				out << "struct " << name << "_out {\n";
				++indentation;
				i = 0;
				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					unsigned id = v->first;
					Variable& variable = v->second;
				
					Type t = types[variable.type];
					Name n = names[id];
				
					if (variable.storage == StorageClassOutput) {
						if (variable.builtin && stage == EShLangVertex) {
							positionName = n.name;
							indent(out);
							out << t.name << " " << n.name << " [[position]];\n";
						}
						else if (variable.builtin && stage == EShLangFragment) {
							indent(out);
							out << t.name << " " << n.name << " : COLOR;\n";
						}
						else {
							indent(out);
							out << t.name << " " << n.name << ";\n";
							++i;
						}
					}
				}
				--indentation;
				indent(out);
				out << "};\n\n";
			}
			
			indent(out);
			if (stage == EShLangVertex) {
				out << "vertex " << name << "_out " << name << "_main(device " << name << "_in* vertices [[buffer(0)]]"
					<< ", constant " << name << "_uniforms& uniforms [[buffer(1)]]"
					<< ", unsigned int vid [[vertex_id]]) {\n";
			}
			else {
				out << "fragment float4 " << name << "_main(constant " << name << "_uniforms& uniforms [[buffer(0)]]"
					<< ", " << name << "_in input [[stage_in]]";
				
				int texindex = 0;
				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					unsigned id = v->first;
					Variable& variable = v->second;
					
					Type t = types[variable.type];
					Name n = names[id];
					
					if (variable.storage == StorageClassUniformConstant) {
						if (strcmp(t.name, "sampler2D") == 0) {
							indent(out);
							out << ", texture2d<float> " << n.name << " [[texture(" << texindex << ")]]"
								<< ", sampler " << n.name << "Sampler [[sampler(" << texindex << ")]]";
							++texindex;
						}
					}
				}
				
				out << ") {\n";
			}
			++indentation;
			indent(out);
			if (stage == EShLangVertex) out << name << "_out output;";
			else out << "float4 output;";
			break;
		}
		case OpCompositeConstruct: {
			//Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			std::stringstream str;
			str << "float4(" << getReference(inst.operands[2]) << ", " << getReference(inst.operands[3]) << ", " << getReference(inst.operands[4]) << ", " << getReference(inst.operands[5]) << ")";
			references[result] = str.str();
			break;
		}
		case OpMatrixTimesVector: {
			//Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id matrix = inst.operands[2];
			id vector = inst.operands[3];
			std::stringstream str;
			str << "(" << getReference(matrix) << " * " << getReference(vector) << ")";
			references[result] = str.str();
			break;
		}
		case OpTextureSample: {
			//Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id sampler = inst.operands[2];
			id coordinate = inst.operands[3];
			std::stringstream str;
			str << getReference(sampler) << ".sample(" << getReference(sampler) << "Sampler, " << getReference(coordinate) << ")";
			references[result] = str.str();
			break;
		}
		case OpReturn:
			output(out);
			if (stage == EShLangVertex) {
				if (target.version == 9) {
					//out << "output." << positionName << ".x = output." << positionName << ".x - dx_ViewAdjust.x * output." << positionName << ".w;\n";
					//indent(out);
					//out << "output." << positionName << ".y = output." << positionName << ".y + dx_ViewAdjust.y * output." << positionName << ".w;\n";
					//indent(out);
				}
				out << "output." << positionName << ".z = (output." << positionName << ".z + output." << positionName << ".w) * 0.5;\n";
				indent(out);
			}
			out << "return output;";
			break;
		case OpStore: {
			output(out);
			Variable& v = variables[inst.operands[0]];
			if (!v.declared) {
				out << types[v.type].name << " " << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
				v.declared = true;
			}
			else {
				out << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			}
			break;
		}
		default:
			CStyleTranslator::outputInstruction(target, attributes, inst);
			break;
	}
}
