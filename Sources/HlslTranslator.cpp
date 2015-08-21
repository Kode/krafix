#include "HlslTranslator.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <string.h>

using namespace krafix;

typedef unsigned id;

namespace {
	std::string positionName = "gl_Position";
	std::map<unsigned, Name> currentNames;

	bool compareVariables(const Variable& v1, const Variable& v2) {
		Name n1 = currentNames[v1.id];
		Name n2 = currentNames[v2.id];
		return strcmp(n1.name, n2.name) < 0;
	}
}

void HlslTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	std::ofstream file;
	file.open(filename, std::ios::binary | std::ios::out);
	out = &file;

	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		outputInstruction(target, attributes, inst);
		if (outputting) (*out) << "\n";
	}
	for (unsigned i = 0; i < functions.size(); ++i) {
		(*out) << functions[i]->text.str();
		(*out) << "\n\n";
	}

	file.close();
}

void HlslTranslator::outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint) {
	id result = inst.operands[1];
	switch (entrypoint) {
	case GLSLstd450InverseSqrt: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "rsqrt(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Fract: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "frac(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Mix: {
		std::stringstream str;
		id x = inst.operands[4];
		id y = inst.operands[5];
		id a = inst.operands[6];
		str << "lerp(" << getReference(x) << ", " << getReference(y) << ", " << getReference(a) << ")";
		references[result] = str.str();
		break;
	}
	default:
		CStyleTranslator::outputLibraryInstruction(target, attributes, inst, entrypoint);
		break;
	}
}

void HlslTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {
	case OpLabel: {
		if (firstLabel) {
			output(out);
			if (firstFunction) {
				(*out) << "float mod(float x, float y) {\n";
				(*out) << "\treturn x - y * floor(x / y);\n";
				(*out) << "}\n\n";

				if (stage == EShLangVertex && target.version == 9) {
					(*out) << "uniform float4 dx_ViewAdjust;";
				}
				(*out) << "\n";

				std::vector<Variable> sortedVariables;
				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					sortedVariables.push_back(v->second);
				}
				currentNames = names;
				std::sort(sortedVariables.begin(), sortedVariables.end(), compareVariables);

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassUniformConstant) {
						indent(out);
						if (t.isarray) {
							(*out) << "uniform " << t.name << " " << n.name << "[" << t.length << "];\n";
						}
						else {
							(*out) << "uniform " << t.name << " " << n.name << ";\n";
						}
					}
					else {
						if (t.isarray) {
							(*out) << "static " << t.name << " " << n.name << "[" << t.length << "];\n";
						}
						else {
							(*out) << "static " << t.name << " " << n.name << ";\n";
						}
					}
				}
				(*out) << "\n";

				if (stage == EShLangFragment) {
					(*out) << "struct Input2 {\n";
				}
				else {
					(*out) << "struct Input {\n";
				}
				++indentation;
				if (stage == EShLangFragment && target.version > 9) {
					indent(out);
					(*out) << "float4 gl_Position : SV_POSITION;\n";
				}
				else if (stage == EShLangFragment && target.version == 9 && target.system == Unity) {
					indent(out);
					(*out) << "float4 gl_Position : POSITION;\n";
				}
				int uvindex = 0;
				int threeindex = 0;
				int index = 0;
				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassInput) {
						indent(out);
						if (stage == EShLangVertex && target.system == Unity) {
							if (t.name == "float") {
								(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
								++uvindex;
							}
							else if (t.name == "float2") {
								(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
								++uvindex;
							}
							else if (t.name == "float3") {
								if (threeindex == 0) {
									(*out) << t.name << " " << n.name << " : POSITION;\n";
								}
								else {
									(*out) << t.name << " " << n.name << " : NORMAL;\n";
								}
								++threeindex;
							}
							else if (t.name == "float4") {
								(*out) << t.name << " " << n.name << " : TANGENT;\n";
							}
						}
						else {
							(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
							if (stage == EShLangVertex) {
								attributes[n.name] = index;
							}
						}
						++index;
					}
				}
				--indentation;
				indent(out);
				(*out) << "};\n\n";

				indent(out);
				if (stage == EShLangFragment) {
					(*out) << "struct Output2 {\n";
				}
				else {
					(*out) << "struct Output {\n";
				}
				++indentation;
				index = 0;

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassOutput) {
						if (variable.builtin && stage == EShLangVertex) {
							positionName = n.name;
							indent(out);
							if (target.version == 9) {
								(*out) << t.name << " " << n.name << " : POSITION;\n";
							}
							else {
								(*out) << t.name << " " << n.name << " : SV_POSITION;\n";
							}
						}
						else if (stage == EShLangFragment) {
							indent(out);
							(*out) << t.name << " " << n.name << " : COLOR;\n";
						}
					}
				}

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassOutput) {
						if (variable.builtin && stage == EShLangVertex) {

						}
						else if (stage == EShLangFragment) {

						}
						else {
							indent(out);
							(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
							++index;
						}
					}
				}

				--indentation;
				indent(out);
				(*out) << "};\n\n";

				if (stage == EShLangFragment) {
					(*out) << "void frag_main();\n\n";
				}
				else {
					(*out) << "void vert_main();\n\n";
				}

				if (target.system == Unity) {
					if (stage == EShLangFragment) {
						(*out) << "Output2 frag(Input2 input)\n";
					}
					else {
						(*out) << "Output vert(Input input)\n";
					}
				}
				else {
					if (stage == EShLangFragment) {
						(*out) << "Output2 main(Input2 input)\n";
					}
					else {
						(*out) << "Output main(Input input)\n";
					}
				}

				indent(out);
				(*out) << "{\n";
				++indentation;

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassInput) {
						indent(out);
						(*out) << n.name << " = input." << n.name << ";\n";
					}
				}

				indent(out);
				if (stage == EShLangFragment) {
					(*out) << "frag_main();\n";
				}
				else {
					(*out) << "vert_main();\n";
				}
				indent(out);
				if (stage == EShLangFragment) {
					(*out) << "Output2 output;\n";
				}
				else {
					(*out) << "Output output;\n";
				}

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassOutput) {
						indent(out);
						(*out) << "output." << n.name << " = " << n.name << ";\n";
					}
				}

				indent(out);
				if (stage == EShLangVertex) {
					if (target.version == 9 && target.system != Unity) {
						(*out) << "output." << positionName << ".x = output." << positionName << ".x - dx_ViewAdjust.x * output." << positionName << ".w;\n";
						indent(out);
						(*out) << "output." << positionName << ".y = output." << positionName << ".y + dx_ViewAdjust.y * output." << positionName << ".w;\n";
						indent(out);
					}
					(*out) << "output." << positionName << ".z = (output." << positionName << ".z + output." << positionName << ".w) * 0.5;\n";
					indent(out);
				}
				(*out) << "return output;\n";

				--indentation;
				(*out) << "}\n\n";

				firstFunction = false;
			}

			if (funcName != "main") {
				(*out) << funcType << " " << funcName << "(";
				for (unsigned i = 0; i < parameters.size(); ++i) {
					(*out) << parameters[i].type.name << " " << getReference(parameters[i].id);
					if (i < parameters.size() - 1) (*out) << ", ";
				}
				(*out) << ");\n";
			}

			startFunction(funcName);

			if (funcName == "main") {
				if (stage == EShLangFragment) {
					(*out) << "void frag_main()\n";
				}
				else {
					(*out) << "void vert_main()\n";
				}
			}
			else {
				(*out) << funcType << " " << funcName << "(";
				for (unsigned i = 0; i < parameters.size(); ++i) {
					(*out) << parameters[i].type.name << " " << getReference(parameters[i].id);
					if (i < parameters.size() - 1) (*out) << ", ";
				}
				(*out) << ")\n";
			}
			indent(out);
			(*out) << "{";
			++indentation;

			firstLabel = false;
		}
		else {
			CStyleTranslator::outputInstruction(target, attributes, inst);
		}
		break;
	}
	case OpExecutionMode:
		break;
	case OpTypeArray: {
		Type t;
		t.isarray = true;
		unsigned id = inst.operands[0];
		t.name = "unknownarray";
		Type subtype = types[inst.operands[1]];
		t.length = atoi(references[inst.operands[2]].c_str());
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "float") == 0) {
				t.name = "float";
			}
			else if (strcmp(subtype.name, "float2") == 0) {
				t.name = "float2";
			}
			else if (strcmp(subtype.name, "float3") == 0) {
				t.name = "float3";
			}
			else if (strcmp(subtype.name, "float4") == 0) {
				t.name = "float4";
			}
		}
		types[id] = t;
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
		t.name = "float4x?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "float3") == 0 && inst.operands[2] == 3) {
				t.name = "float3x3";
				t.length = 4;
				types[id] = t;
			}
			if (strcmp(subtype.name, "float4") == 0 && inst.operands[2] == 4) {
				t.name = "float4x4";
				t.length = 4;
				types[id] = t;
			}
		}
		break;
	}
	case OpTypeImage: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "sampler2D";
		types[id] = t;
		break;
	} 
	case OpVariable: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		Variable& v = variables[result];
		v.id = result;
		v.type = inst.operands[0];
		v.storage = (StorageClass)inst.operands[2];
		v.declared = true; // v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(result) != names.end()) {
			references[result] = names[result].name;
		}
		if (v.storage == StorageClassFunction && getReference(result) != "param") {
			output(out);
			Type t = types[v.type];
			if (t.isarray) {
				(*out) << t.name << " " << getReference(result) << "[" << t.length << "];\n";
			}
			else {
				(*out) << t.name << " " << getReference(result) << ";";
			}
		}
		break;
	}
	case OpCompositeConstruct: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		std::stringstream str;
		str << resultType.name << "(";
		for (unsigned i = 2; i < inst.length; ++i) {
			str << getReference(inst.operands[i]);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpMatrixTimesVector: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id matrix = inst.operands[2];
		id vector = inst.operands[3];
		std::stringstream str;
		str << "mul(transpose(" << getReference(matrix) << "), " << getReference(vector) << ")"; // TODO: Get rid of transpose, when kfx is deprecated
		references[result] = str.str();
		break;
	}
	case OpVectorTimesMatrix: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector = inst.operands[2];
		id matrix = inst.operands[3];
		std::stringstream str;
		str << "mul(" << getReference(vector) << ", transpose(" << getReference(matrix) << "))";
		references[result] = str.str();
		break;
	}
	case OpMatrixTimesMatrix: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "transpose(mul(transpose(" << getReference(operand1) << "), transpose(" << getReference(operand2) << ")))";
		references[result] = str.str();
		break;
	}
	case OpImageSampleImplicitLod: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id sampler = inst.operands[2];
		id coordinate = inst.operands[3];
		std::stringstream str;
		if (target.system == Unity) {
			str << "tex2D(" << getReference(sampler) << ", float2(" << getReference(coordinate) << ".x, 1.0 - " << getReference(coordinate) << ".y))";
		}
		else {
			str << "tex2D(" << getReference(sampler) << ", " << getReference(coordinate) << ")";
		}
		references[result] = str.str();
		break;
	}
	case OpReturn:
		output(out);
		(*out) << "return;";
		break;
	case OpStore: {
		Variable& v = variables[inst.operands[0]];
		if (getReference(inst.operands[0]) == "param") {
			references[inst.operands[0]] = getReference(inst.operands[1]);
		}
		else {
			output(out);
			if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
				(*out) << getReference(inst.operands[0]) << indexName(compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
			}
			else {
				(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			}
		}
		break;
	}
	default:
		CStyleTranslator::outputInstruction(target, attributes, inst);
		break;
	}
}
