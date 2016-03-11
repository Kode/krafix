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
		case OpExecutionMode: {
			ExecutionMode execMode = (ExecutionMode)inst.operands[1];
			switch (execMode) {
				case ExecutionModeInvocations:
					executionModes.invocationCount = inst.operands[2];
					break;
				case ExecutionModeSpacingEqual:
				case ExecutionModeSpacingFractionalEven:
				case ExecutionModeSpacingFractionalOdd:
					executionModes.spacingType = execMode;
					break;
				case ExecutionModeVertexOrderCw:
				case ExecutionModeVertexOrderCcw:
					executionModes.vertexOrder = execMode;
					break;
				case ExecutionModePixelCenterInteger:
					executionModes.usePixelCenterInteger = true;
					break;
				case ExecutionModeOriginUpperLeft:
				case ExecutionModeOriginLowerLeft:
					executionModes.originOrientation = execMode;
					break;
				case ExecutionModeEarlyFragmentTests:
					executionModes.useEarlyFragmentTests = true;
					break;
				case ExecutionModePointMode:
					executionModes.useTessellationPoints = true;
					break;
				case ExecutionModeXfb:
					executionModes.useTransformFeedback = true;
					break;
				case ExecutionModeDepthReplacing:
					executionModes.useDepthModification = true;
					break;
				case ExecutionModeDepthGreater:
				case ExecutionModeDepthLess:
				case ExecutionModeDepthUnchanged:
					executionModes.depthModificationType = execMode;
					break;
				case ExecutionModeLocalSize:
					executionModes.localSize[0] = inst.operands[2];
					executionModes.localSize[1] = inst.operands[3];
					executionModes.localSize[2] = inst.operands[4];
					break;
				case ExecutionModeLocalSizeHint:
					executionModes.localSizeHint[0] = inst.operands[2];
					executionModes.localSizeHint[1] = inst.operands[3];
					executionModes.localSizeHint[2] = inst.operands[4];
					break;
				case ExecutionModeInputPoints:
				case ExecutionModeInputLines:
				case ExecutionModeInputLinesAdjacency:
				case ExecutionModeTriangles:
				case ExecutionModeInputTrianglesAdjacency:
				case ExecutionModeQuads:
				case ExecutionModeIsolines:
					executionModes.primitiveType = execMode;
					break;
				case ExecutionModeOutputVertices:
				case ExecutionModeOutputPoints:
				case ExecutionModeOutputLineStrip:
				case ExecutionModeOutputTriangleStrip:
					executionModes.outputPrimitiveType = execMode;
					break;
				case ExecutionModeVecTypeHint:
					executionModes.vectorTypeHint = inst.operands[2];
					break;
				case ExecutionModeContractionOff:
					executionModes.disallowContractions = true;
					break;
				default:
					(*out) << "// Unknown execution mode";
			}
			break;
		}
		case OpAccessChain: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			id base = inst.operands[2];
			std::stringstream str;
			str << getReference(base);

			unsigned typeId = getBaseTypeID(variables[base].type);
			for (unsigned i = 3; i < inst.length; ++i) {
				Type t = types[typeId];
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
			Type t = types[reftype];								// Pass through referenced type
			t.opcode = inst.opcode;									// ...except OpCode
			t.baseType = reftype;									// ...and base type
			t.length = atoi(references[inst.operands[2]].c_str());	// ...and length
			t.byteSize = t.byteSize * t.length;						// ...and byte size
			t.isarray = true;										// ...and array marker
			types[id] = t;
			break;
		}
		case OpTypeVector: {
			unsigned id = inst.operands[0];
			Type& t = types[id];
			t.opcode = inst.opcode;
			t.length = inst.operands[2];
			Type subtype = types[inst.operands[1]];
			t.name = subtype.name + std::to_string(t.length);
			t.byteSize = subtype.byteSize * t.length;
			break;
		}
		case OpTypeMatrix: {
			unsigned id = inst.operands[0];
			Type& t = types[id];
			t.opcode = inst.opcode;
			t.length = inst.operands[2];
			Type& subtype = types[inst.operands[1]];
			t.name = "float" + std::to_string(t.length) + "x" + std::to_string(subtype.length);
			t.byteSize = subtype.byteSize * t.length;
			break;
		}
		case OpTypeImage: {
			unsigned id = inst.operands[0];
			Type& t = types[id];
			t.opcode = inst.opcode;
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

			break;
		}
		case OpTypeSampler: {
			unsigned id = inst.operands[0];
			Type& t = types[id];
			t.opcode = inst.opcode;
			t.name = "sampler2D";
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
						Type& type = types[v.type];
						if (type.name == "float2" || type.name == "float3" || type.name == "float4") references[id] = type.name + std::string("(vertices[vid].") + names[id].name + ")";
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
					Type& type = types[v.type];
					if (type.name == "sampler2D") references[id] = names[id].name;
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

				Type& t = types[variable.type];
				Name n = names[id];

				if (variable.storage == StorageClassUniformConstant) {
					if (t.name != "sampler2D") {
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

				Type& t = types[variable.type];
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

					Type& t = types[variable.type];
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

					Type& t = types[variable.type];
					Name n = names[id];

					if (variable.storage == StorageClassUniformConstant) {
						if (t.name == "sampler2D") {
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
				Type& t = getBaseType(v.type);
				(*out) << t.name << " "<< getReference(refId);
				if (t.isarray) { (*out) << "[" << t.length << "]"; }

			} else {
				(*out) << getReference(refId);
			}
			(*out) << " = " << getReference(inst.operands[1]) << ";";
			break;
		}
		case OpConstantComposite: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;

			std::stringstream str;
			str << "{";
			for (unsigned i = 2; i < inst.length; ++i) {
				str << getReference(inst.operands[i]);
				if (i < inst.length - 1) str << ", ";
			}
			str << "}";

			references[result] = str.str();
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
		case BuiltInVertexId:		return "vertex_id";
		case BuiltInVertexIndex:	return "vertex_id";
		case BuiltInInstanceId:		return "instance_id";
		case BuiltInInstanceIndex:	return "instance_id";

		// Vertex function out
		case BuiltInClipDistance:	return "clip_distance";
		case BuiltInPointSize:		return "point_size";
		case BuiltInPosition:		return "position";

		// Fragment function in
		case BuiltInFrontFacing:	return "front_facing";
		case BuiltInPointCoord:		return "point_coord";
		case BuiltInSamplePosition:	return "position";
		case BuiltInSampleId:		return "sample_id";
		case BuiltInSampleMask:		return "sample_mask";

		// Fragment function out
		case BuiltInFragDepth: {
			switch (executionModes.depthModificationType) {
				case ExecutionModeDepthGreater:
					return "depth(greater)";
				case ExecutionModeDepthLess:
					return "depth(less)";
				case ExecutionModeDepthUnchanged:
				default:
					return "depth(any)";
			}
		}

		default: return "unsupported-built-in";
	}
}

std::string MetalTranslator::builtInTypeName(Variable& variable) {
	using namespace spv;
	switch (variable.builtinType) {
		case BuiltInVertexId:
		case BuiltInVertexIndex:
		case BuiltInInstanceId:
		case BuiltInInstanceIndex:
			return "uint";
		default: {
			return getBaseType(variable.type).name;
		}
	}
}
