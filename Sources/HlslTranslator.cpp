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

	file.close();
}

void HlslTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
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
		t.name = "float4x?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name != NULL) {
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
		unsigned id = inst.operands[1];
		Variable& v = variables[id];
		v.id = id;
		v.type = inst.operands[0];
		v.storage = (StorageClass)inst.operands[2];
		v.declared = v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(id) != names.end()) {
			if (v.storage == StorageClassInput) {
				references[id] = std::string("input.") + names[id].name;
			}
			else if (v.storage == StorageClassOutput) {
				references[id] = std::string("output.") + names[id].name;
			}
			else {
				references[id] = names[id].name;
			}
		}
		break;
	}
	case OpFunction: {
		output(out);
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

		indent(out);
		(*out) << "Output main(Input input) {\n";
		++indentation;
		indent(out);
		(*out) << "Output output;\n";
		for (unsigned i = 0; i < sortedVariables.size(); ++i) {
			Variable variable = sortedVariables[i];
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
		break;
	}
	case OpCompositeConstruct: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		std::stringstream str;
		str << "float4(" << getReference(inst.operands[2]) << ", " << getReference(inst.operands[3]) << ", " << getReference(inst.operands[4]) << ", " << getReference(inst.operands[5]) << ")";
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
		output(out);
		Variable& v = variables[inst.operands[0]];
		if (!v.declared) {
			(*out) << types[v.type].name << " " << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			v.declared = true;
		}
		else {
			(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
		}
		break;
	}
	default:
		CStyleTranslator::outputInstruction(target, attributes, inst);
		break;
	}
}
