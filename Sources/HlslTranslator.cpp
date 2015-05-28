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

void HlslTranslator::outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSL_STD_450::Entrypoints entrypoint) {
	using namespace GLSL_STD_450;
	id result = inst.operands[1];
	switch (entrypoint) {
	case InverseSqrt: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "rsqrt(" << getReference(x) << ")";
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
						(*out) << "uniform " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage != StorageClassInput && variable.storage != StorageClassOutput) {
						(*out) << "static " << t.name << " " << n.name << ";\n";
					}
				}
				(*out) << "\n";

				(*out) << "struct Input {\n";
				++indentation;
				if (stage == EShLangFragment && target.version > 9) {
					indent(out);
					(*out) << "float4 gl_Position : SV_POSITION;\n";
				}
				int index = 0;
				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassInput) {
						indent(out);
						(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
						if (stage == EShLangVertex) {
							attributes[n.name] = index;
						}
						++index;
					}
				}
				--indentation;
				indent(out);
				(*out) << "};\n\n";

				indent(out);
				(*out) << "struct Output {\n";
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
						else if (variable.builtin && stage == EShLangFragment) {
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
						else if (variable.builtin && stage == EShLangFragment) {

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
				(*out) << "Output main(Input input)\n";
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

			if (funcName == "main") {
				(*out) << "\n";
				indent(out);
				(*out) << "Output output;\n";
				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					Variable variable = v->second;
					if (variable.storage == StorageClassOutput) {
						Type t = types[variable.type];
						Name n = names[variable.id];
						indent(out);
						(*out) << "output." << n.name << " = ";
						if (t.name == "float") (*out) << "0.0;\n";
						if (t.name == "float2") (*out) << "float2(0.0, 0.0);\n";
						if (t.name == "float3") (*out) << "float3(0.0, 0.0, 0.0);\n";
						if (t.name == "float4") (*out) << "float4(0.0, 0.0, 0.0, 0.0);\n";
					}
				}
			}

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
	case OpTypeSampler: {
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
			if (v.storage == StorageClassInput) {
				references[result] = std::string("input.") + names[result].name;
			}
			else if (v.storage == StorageClassOutput) {
				references[result] = std::string("output.") + names[result].name;
			}
			else {
				references[result] = names[result].name;
			}
		}
		if (v.storage == StorageClassFunction && getReference(result) != "param") {
			output(out);
			(*out) << types[v.type].name << " " << getReference(result) << ";";
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
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "transpose(mul(transpose(" << getReference(operand1) << "), transpose(" << getReference(operand2) << ")))";
		references[result] = str.str();
		break;
	}
	case OpTextureSample: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id sampler = inst.operands[2];
		id coordinate = inst.operands[3];
		std::stringstream str;
		str << "tex2D(" << getReference(sampler) << ", " << getReference(coordinate) << ")";
		references[result] = str.str();
		break;
	}
	case OpReturn:
		output(out);
		if (stage == EShLangVertex) {
			if (target.version == 9) {
				(*out) << "output." << positionName << ".x = output." << positionName << ".x - dx_ViewAdjust.x * output." << positionName << ".w;\n";
				indent(out);
				(*out) << "output." << positionName << ".y = output." << positionName << ".y + dx_ViewAdjust.y * output." << positionName << ".w;\n";
				indent(out);
			}
			(*out) << "output." << positionName << ".z = (output." << positionName << ".z + output." << positionName << ".w) * 0.5;\n";
			indent(out);
		}
		(*out) << "return output;";
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
