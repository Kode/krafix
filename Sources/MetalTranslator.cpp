#include "MetalTranslator.h"
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string.h>

using namespace krafix;

typedef unsigned id;

namespace {
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
		for (unsigned i = 0; i < str.length(); ++i) {
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

	std::ofstream file;
	file.open(filename, std::ios::binary | std::ios::out);
	out = &file;

	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		outputInstruction(target, attributes, inst);
		if (outputting) (*out) << "\n";
	}

	file.close();
}

void MetalTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {
		case OpExecutionMode:
			break;
		case OpAccessChain: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			id base = inst.operands[2];
			std::stringstream str;
			str << getReference(base);

			unsigned typeId = variables[base].type;
			Type t = types[typeId];
			if (t.opcode == OpTypePointer) { typeId = t.baseType; }

			for (unsigned i = 3; i < inst.length; ++i) {
				t = types[typeId];
				unsigned elemRef = inst.operands[i];
				switch (t.opcode) {
					case OpTypeStruct: {
						unsigned mbrIdx = atoi(references[elemRef].c_str());
						unsigned mbrId = getMemberId(typeId, mbrIdx);
						str << "." << getReference(mbrId);
						Member mbr = members[mbrId];
						typeId = mbr.type;
						break;
					}
					case OpTypeArray: {
						str << "[" << getReference(elemRef) << "]";
						typeId = t.baseType;
						break;
					}
					default:
						str << "[" << getReference(elemRef) << "]";
						break;
				}
			}
			references[result] = str.str();
			break;
		}
		case OpTypeArray: {
			unsigned id = inst.operands[0];
			unsigned reftype = inst.operands[1];
			Type t = types[reftype];		// Pass through referenced type
			t.opcode = inst.opcode;			// ...except OpCode
			t.baseType = reftype;			// ...and base type
			t.length = inst.operands[2];	// ...and length
			t.isarray = true;				// ...and array marker	
			types[id] = t;
			break;
		}
		case OpTypeVector: {
			Type t(inst.opcode);
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
			Type t(inst.opcode);
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
		case OpTypeImage: {
			Type t(inst.opcode);
			unsigned id = inst.operands[0];
			t.imageDim = (spv::Dim)inst.operands[2];
			t.isDepthImage = (inst.operands[3] == 1);
			t.isarray = !!(inst.operands[4]);
			t.isMultiSampledImage = !!(inst.operands[5]);
			t.sampledImage = (SampledImage)inst.operands[6];

			if (t.isDepthImage) {
				switch (t.imageDim) {
					case spv::Dim2D:
						t.name = t.isMultiSampledImage ? "depth2d_ms" : (t.isarray ? "depth2d_array" : "depth2d");
						break;
					case spv::DimCube:
						t.name = t.isarray ? "depthcube_array" : "depthcube";
						break;
					default:
						break;
				}
			} else {
				switch (t.imageDim) {
					case spv::Dim1D:
						t.name = t.isarray ? "texture1d_array" : "texture1d";
						break;
					case spv::Dim2D:
						t.name = t.isMultiSampledImage ? "texture2d_ms" : (t.isarray ? "texture2d_array" : "texture2d");
						break;
					case spv::Dim3D:
						t.name = "texture3D";
						break;
					case spv::DimCube:
						t.name = t.isarray ? "texturecube_array" : "texturecube";
						break;
					default:
						break;
				}
			}

			types[id] = t;
			break;
		}
		case OpTypeSampler: {
			Type t(inst.opcode);
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
			(*out) << "#include <metal_stdlib>\n";
			(*out) << "#include <simd/simd.h>\n";
			(*out) << "\n";
			(*out) << "using namespace metal;\n";
			(*out) << "\n";
			indent(out);
			(*out) << "struct " << name << "_uniforms {\n";
			++indentation;
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;

				Type t = types[variable.type];
				Name n = names[id];

				if (variable.storage == StorageClassUniformConstant) {
					if (strcmp(t.name, "sampler2D") != 0) {
						indent(out);
						(*out) << t.name << " " << n.name << ";\n";
					}
				}
			}
			--indentation;
			indent(out);
			(*out) << "};\n\n";

			(*out) << "struct " << name << "_in {\n";
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
						(*out) << "packed_" << t.name << " " << n.name << ";\n";
					}
					else {
						(*out) << t.name << " " << n.name << ";\n";
					}
					++i;
				}
			}
			--indentation;
			indent(out);
			(*out) << "};\n\n";

			if (stage == EShLangVertex) {
				indent(out);
				(*out) << "struct " << name << "_out {\n";
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
							(*out) << t.name << " " << n.name << " [[position]];\n";
						}
						else if (variable.builtin && stage == EShLangFragment) {
							indent(out);
							(*out) << t.name << " " << n.name << " : COLOR;\n";
						}
						else {
							indent(out);
							(*out) << t.name << " " << n.name << ";\n";
							++i;
						}
					}
				}
				--indentation;
				indent(out);
				(*out) << "};\n\n";
			}

			indent(out);
			if (stage == EShLangVertex) {
				(*out) << "vertex " << name << "_out " << name << "_main(device " << name << "_in* vertices [[buffer(0)]]"
					<< ", constant " << name << "_uniforms& uniforms [[buffer(1)]]"
					<< ", unsigned int vid [[vertex_id]]) {\n";
			}
			else {
				(*out) << "fragment float4 " << name << "_main(constant " << name << "_uniforms& uniforms [[buffer(0)]]"
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
							(*out) << ", texture2d<float> " << n.name << " [[texture(" << texindex << ")]]"
								<< ", sampler " << n.name << "Sampler [[sampler(" << texindex << ")]]";
							++texindex;
						}
					}
				}

				(*out) << ") {\n";
			}
			++indentation;
			indent(out);
			if (stage == EShLangVertex) (*out) << name << "_out output;";
			else (*out) << "float4 output;";
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
		case OpImageSampleImplicitLod: {
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
				(*out) << "output." << positionName << ".z = (output." << positionName << ".z + output." << positionName << ".w) * 0.5;\n";
				indent(out);
			}
			(*out) << "return output;";
			break;
		case OpStore: {
			output(out);
			unsigned refId = inst.operands[0];
			Variable& v = variables[refId];
			if (v.type != 0 && !v.declared) {
				(*out) << types[v.type].name << " ";
				v.declared = true;
			}
			(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			break;
		}
		default:
			CStyleTranslator::outputInstruction(target, attributes, inst);
			break;
	}
}

const char* MetalTranslator::builtInName(spv::BuiltIn builtin) {
	using namespace spv;
	switch (builtin) {
		// Vertex function in
		case BuiltInVertexId: return "vertex_id";
		case BuiltInInstanceId: return "instance_id";

		// Vertex function out
		case BuiltInClipDistance: return "clip_distance";
		case BuiltInPointSize: return "point_size";
		case BuiltInPosition: return "position";

		// Fragment function in
		case BuiltInFrontFacing: return "front_facing";
		case BuiltInPointCoord: return "point_coord";
		case BuiltInSamplePosition: return "position";
		case BuiltInSampleId: return "sample_id";
		case BuiltInSampleMask: return "sample_mask";

		// Fragment function out
		case BuiltInFragDepth: return "depth(any)";

		default: return "unsupported-built-in";
	}
}

const char* MetalTranslator::builtInTypeName(spv::BuiltIn builtin, Type& type) {
	using namespace spv;
	switch (builtin) {
			// Vertex function in
		case BuiltInVertexId: return "uint";
		case BuiltInInstanceId: return "uint";
		default: return type.name;
	}
}


