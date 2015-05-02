#include "HlslTranslator.h"
#include <fstream>
#include <map>
#include <sstream>

using namespace krafix;

typedef unsigned id;

namespace {
	struct Variable {
		unsigned type;
		spv::StorageClass storage;
		bool builtin;
		bool declared;

		Variable() : builtin(false) {}
	};

	struct Type {
		const char* name;
		unsigned length;

		Type() : name("unknown"), length(1) {}
	};

	struct Name {
		const char* name;
	};

	const char* indexName(unsigned index) {
		switch (index) {
		case 0:
			return "x";
		case 1:
			return "y";
		case 2:
			return "z";
		case 3:
		default:
			return "w";
		}
	}

	int indentation = 0;

	void indent(std::ofstream& out) {
		for (int i = 0; i < indentation; ++i) {
			out << "\t";
		}
	}

	bool outputting = false;

	void output(std::ofstream& out) {
		outputting = true;
		indent(out);
	}

	std::map<id, std::string> references;

	std::string getReference(id _id) {
		if (references.find(_id) == references.end()) {
			std::stringstream str;
			str << "_" << _id;
			return str.str();
		}
		else {
			return references[_id];
		}
	}
}

void HlslTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Type> types;
	std::map<unsigned, Variable> variables;
	std::map<id, std::string> labelStarts;
	std::map<id, int> merges;

	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		case OpName: {
			Name n;
			unsigned id = inst.operands[0];
			n.name = inst.string;
			names[id] = n;
			break;
		}
		case OpTypePointer: {
			Type t;
			unsigned id = inst.operands[0];
			Type subtype = types[inst.operands[2]];
			t.name = subtype.name;
			types[id] = t;
			break;
		}
		case OpTypeFloat: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "float";
			types[id] = t;
			break;
		}
		case OpTypeInt: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "int";
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
		case OpConstant: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			std::string value = "unknown";
			if (resultType.name == "float") {
				float f = *(float*)&inst.operands[2];
				std::stringstream strvalue;
				strvalue << f;
				if (strvalue.str().find('.') == std::string::npos) strvalue << ".0";
				value = strvalue.str();
			}
			if (resultType.name == "int") {
				std::stringstream strvalue;
				strvalue << *(int*)&inst.operands[2];
				value = strvalue.str();
			}
			references[result] = value;
			break;
		}
		case OpConstantComposite: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
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
		case OpVariable: {
			unsigned id = inst.operands[1];
			Variable& v = variables[id];
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
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;

				Type t = types[variable.type];
				Name n = names[id];

				if (variable.storage == StorageClassUniformConstant) {
					indent(out);
					out << "uniform " << t.name << " " << n.name << ";\n";
				}
			}
			out << "\n";
			
			out << "struct Input {\n";
			++indentation;
			int i = 0;
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;

				Type t = types[variable.type];
				Name n = names[id];

				if (variable.storage == StorageClassInput) {
					if (variable.builtin && stage == EShLangVertex) {
						indent(out);
						out << t.name << " " << n.name << " : POSITION;\n";
					}
					else {
						indent(out);
						out << t.name << " " << n.name << " : TEXCOORD" << i << ";\n";
						if (stage == EShLangVertex) {
							attributes[n.name] = i;
						}
						++i;
					}
				}
			}
			--indentation;
			indent(out);
			out << "};\n\n";

			indent(out);
			out << "struct Output {\n";
			++indentation;
			i = 0;
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;
				
				Type t = types[variable.type];
				Name n = names[id];

				if (variable.storage == StorageClassOutput) {
					if (variable.builtin && stage == EShLangVertex) {
						indent(out);
						out << t.name << " " << n.name << " : POSITION;\n";
					}
					else if (variable.builtin && stage == EShLangFragment) {
						indent(out);
						out << t.name << " " << n.name << " : COLOR;\n";
					}
					else {
						indent(out);
						out << t.name << " " << n.name << " : TEXCOORD" << i << ";\n";
						++i;
					}
				}
			}
			--indentation;
			indent(out);
			out << "};\n\n";

			indent(out);
			out << "Output main(Input input) {\n";
			++indentation;
			indent(out);
			out << "Output output;";
			break;
		}
		case OpFunctionEnd:
			--indentation;
			output(out);
			out << "}";
			break;
		case OpCompositeConstruct: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			std::stringstream str;
			str << "float4(" << getReference(inst.operands[2]) << ", " << getReference(inst.operands[3]) << ", " << getReference(inst.operands[4]) << ", " << getReference(inst.operands[5]) << ")";
			references[result] = str.str();
			break;
		}
		case OpCompositeExtract: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id composite = inst.operands[2];
			std::stringstream str;
			str << getReference(composite) << "." << indexName(inst.operands[3]);
			references[result] = str.str();
			break;
		}
		case OpMatrixTimesVector: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id matrix = inst.operands[2];
			id vector = inst.operands[3];
			std::stringstream str;
			str << "(" << matrix << " * " << vector << ")";
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
		case OpVectorShuffle: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id vector1 = inst.operands[2];
			id vector1length = 4; // types[variables[inst.operands[2]].type].length;
			id vector2 = inst.operands[3];
			id vector2length = 4; // types[variables[inst.operands[3]].type].length;
			std::stringstream str;
			str << resultType.name << "(";
			for (unsigned i = 4; i < inst.length; ++i) {
				id index = inst.operands[i];
				if (index < vector1length) str << getReference(vector1) << "." << indexName(index);
				else str << getReference(vector2) << "." << indexName(index - vector1length);
				if (i < inst.length - 1) str << ", ";
			}
			str << ")";
			references[result] = str.str();
			break;
		}
		case OpFMul: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id operand1 = inst.operands[2];
			id operand2 = inst.operands[3];
			std::stringstream str;
			str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
			references[result] = str.str();
			break;
		}
		case OpVectorTimesScalar: {
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id vector = inst.operands[2];
			id scalar = inst.operands[3];
			std::stringstream str;
			str << "(" << getReference(vector) << " * " << getReference(scalar) << ")";
			references[result] = str.str();
			break;
		}
		case OpReturn:
			output(out);
			out << "return output;";
			break;
		case OpLabel:
			break;
		case OpBranch:
			break;
		case OpDecorate: {
			unsigned target = inst.operands[0];
			Decoration decoration = (Decoration)inst.operands[1];
			if (decoration == DecorationBuiltIn) {
				variables[target].builtin = true;
			}
			break;
		}
		case OpTypeFunction: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "function";
			types[id] = t;
			break;
		}
		case OpTypeVoid:
			break;
		case OpEntryPoint:
			break;
		case OpMemoryModel:
			break;
		case OpExtInstImport:
			break;
		case OpSource:
			break;
		case OpLoad: {
			/*Type t = types[inst.operands[0]];
			if (names.find(inst.operands[2]) != names.end()) {
				Name& n = names[inst.operands[2]];
				Variable& v = variables[inst.operands[2]];
				if (v.storage == StorageClassInput) {
					out << "\t" << t.name << " _" << inst.operands[1] << " = input." << n.name << ";\n";
				}
				else {
					out << "\t" << t.name << " _" << inst.operands[1] << " = " << n.name << ";\n";
				}
			}
			else {
				out << "\t" << t.name << " _" << inst.operands[1] << " = _" << inst.operands[2] << ";\n";
			}*/
			references[inst.operands[1]] = getReference(inst.operands[2]);
			break;
		}
		case OpStore: {
			output(out);
			/*Variable& v = variables[inst.operands[0]];
			if (v.storage == StorageClassOutput) {
				out << "output." << names[inst.operands[0]].name << " = _" << inst.operands[1] << ";\n";
			}
			else {
				out << "\t" << names[inst.operands[0]].name << " = _" << inst.operands[1] << ";\n";
			}*/
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
			output(out);
			out << "// Unknown operation " << inst.opcode;
			break;
		}
		if (outputting) out << "\n";
	}

	out.close();
}
