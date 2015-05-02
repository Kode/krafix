#include "GlslTranslator.h"
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

void GlslTranslator::outputCode(const char* baseName) {
	using namespace spv;

	std::map<unsigned, Name> names;
	std::map<unsigned, Type> types;
	std::map<unsigned, Variable> variables;
	std::map<id, std::string> labelStarts;
	std::map<id, int> merges;

	std::ofstream out;
	std::string fileName(baseName);
	fileName.append(".glsl");
	out.open(fileName.c_str(), std::ios::binary | std::ios::out);
	
	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		switch (inst.opcode) {
		case OpExecutionMode: {
			output(out);
			switch (inst.operands[1]) {
			case ExecutionModeOutputVertices:
				out << "layout(vertices = " << inst.operands[2] << ") out;";
				break;
			default:
				out << "// Unknown execution mode";
			}
			break;
		}
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
		case OpTypeBool: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "bool";
			types[id] = t;
			break;
		}
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
				if (strcmp(subtype.name, "vec3") == 0) {
					t.name = "vec3[]";
					t.length = 2;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeVector: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "vec?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 2) {
					t.name = "vec2";
					t.length = 2;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
					t.name = "vec3";
					t.length = 3;
					types[id] = t;
				}
				else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
					t.name = "vec4";
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeMatrix: {
			Type t;
			unsigned id = inst.operands[0];
			t.name = "mat?";
			Type subtype = types[inst.operands[1]];
			if (subtype.name != NULL) {
				if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
					t.name = "mat4";
					t.length = 4;
					types[id] = t;
				}
			}
			break;
		}
		case OpTypeSampler: {
			Type t;
			unsigned id = inst.operands[0];
			bool video = inst.length >= 8 && inst.operands[8] == 1;
			if (video) {
				t.name = "samplerExternalOES";
			}
			else {
				t.name = "sampler2D";
			}
			types[id] = t;
			break;
		}
		case OpConstant: {
			//output(out);
			Type resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			//out << "const " << resultType.name << " _" << result << " = ";
			std::string value = "unknown";
			if (resultType.name == "float") {
				float f = *(float*)&inst.operands[2];
				std::stringstream strvalue;
				strvalue << f;
				if (strvalue.str().find('.') < 0) strvalue << ".0";
				value = strvalue.str();
			}
			if (resultType.name == "int") {
				std::stringstream strvalue;
				strvalue << *(int*)&inst.operands[2];
				value = strvalue.str();
			}
			references[result] = value;
			//out << ";";
			break;
		}
		case OpVariable: {
			unsigned id = inst.operands[1];
			Variable& v = variables[id];
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			if (names.find(id) != names.end()) {
				references[id] = names[id].name;
			}
			break;
		}
		case OpFunction:
			output(out);
			for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
				unsigned id = v->first;
				Variable& variable = v->second;

				if (variable.builtin) continue;

				Type t = types[variable.type];
				Name n = names[id];
				
				switch (stage) {
				case EShLangVertex:
					if (variable.storage == StorageClassInput) {
						out << "attribute " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassOutput) {
						out << "varying " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassUniformConstant) {
						out << "uniform " << t.name << " " << n.name << ";\n";
					}
					break;
				case EShLangFragment:
					if (variable.storage == StorageClassInput) {
						out << "varying " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassUniformConstant) {
						out << "uniform " << t.name << " " << n.name << ";\n";
					}
					break;
				case EShLangTessControl:
					if (variable.storage == StorageClassInput) {
						out << "in " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassOutput) {
						out << "out " << t.name << " " << n.name << ";\n";
					}
					else if (variable.storage == StorageClassUniformConstant) {
						out << "uniform " << t.name << " " << n.name << ";\n";
					}
					break;
				}
			}
			out << "\n";
			indent(out);
			out << "void main()\n";
			indent(out);
			out << "{";
			++indentation;
			break;
		case OpFunctionEnd:
			--indentation;
			output(out);
			out << "} // end function";
			break;
		case OpCompositeConstruct: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			out << resultType.name << " _" << result << " = vec4(_"
			<< inst.operands[2] << ", _" << inst.operands[3] << ", _"
			<< inst.operands[4] << ", _" << inst.operands[5] << ");";
			break;
		}
		case OpCompositeExtract: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned composite = inst.operands[2];
			out << resultType.name << " _" << result << " = _"
			<< composite << "." << indexName(inst.operands[3]) << ";";
			break;
		}
		case OpMatrixTimesVector: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned matrix = inst.operands[2];
			unsigned vector = inst.operands[3];
			out << resultType.name << " _" << result << " = _" << matrix << " * _" << vector << ";";
			break;
		}
		case OpTextureSample: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned sampler = inst.operands[2];
			unsigned coordinate = inst.operands[3];
			out << resultType.name << " _" << result << " = texture2D(_" << sampler << ", _" << coordinate << ");";
			break;
		}
		case OpVectorShuffle: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector1 = inst.operands[2];
			unsigned vector1length = 4; // types[variables[inst.operands[2]].type].length;
			unsigned vector2 = inst.operands[3];
			unsigned vector2length = 4; // types[variables[inst.operands[3]].type].length;

			out << resultType.name << " _" << result << " = " << resultType.name << "(";
			for (unsigned i = 4; i < inst.length; ++i) {
				unsigned index = inst.operands[i];
				if (index < vector1length) out << "_" << vector1 << "." << indexName(index);
				else out << "_" << vector2 << "." << indexName(index - vector1length);
				if (i < inst.length - 1) out << ", ";
			}
			out << ");";
			break;
		}
		case OpFMul: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			out << resultType.name << " _" << result << " = _" << operand1 << " * _" << operand2 << ";";
			break;
		}
		case OpVectorTimesScalar: {
			output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned vector = inst.operands[2];
			unsigned scalar = inst.operands[3];
			out << resultType.name << " _" << result << " = _" << vector << " * _" << scalar << ";";
			break;
		}
		case OpReturn:
			output(out);
			out << "return;";
			break;
		case OpLabel: {
			id label = inst.operands[0];
			if (merges.find(label) != merges.end()) {
				--indentation;
				output(out);
				out << "} // Label " << label;
			}
			else if (labelStarts.find(inst.operands[0]) != labelStarts.end()) {
				output(out);
				out << labelStarts[inst.operands[0]] << "\n";
				indent(out);
				out << "{ // Label " << label;
				++indentation;
			}
			else {
				output(out);
				out << "// Label " << label;
			}
			break;
		}
		case OpBranch:
			output(out);
			out << "// Branch to " << inst.operands[0];
			break;
		case OpSelectionMerge: {
			output(out);
			id label = inst.operands[0];
			unsigned selection = inst.operands[1];
			out << "// Merge " << label << " " << selection;
			merges[label] = 0;
			break;
		}
		case OpBranchConditional: {
			id condition = inst.operands[0];
			id trueLabel = inst.operands[1];
			id falseLabel = inst.operands[2];
			std::stringstream _true;
			_true << "if (" << getReference(condition) << ")";
			labelStarts[trueLabel] = _true.str();
			labelStarts[falseLabel] = "else";
			break;
		}
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
		case OpIEqual: {
			//output(out);
			Type resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			//out << resultType.name << " _" << result << " = _" << operand1 << " == _" << operand2 << ";";
			std::stringstream str;
			str << getReference(operand1) << " == " << getReference(operand2);
			references[result] = str.str();
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
		case OpAccessChain: {
			//output(out);
			Type t = types[inst.operands[0]];
			id result = inst.operands[1];
			id base = inst.operands[2];
			id index = inst.operands[3];
			//out << t.name << " _" << result << " = _" << base << "[" << getValue(index) << "];";
			std::stringstream str;
			str << getReference(base) << "[" << getReference(index) << "]";
			references[result] = str.str();
			break;
		}
		case OpLoad: {
			//output(out);
			Type t = types[inst.operands[0]];
			references[inst.operands[1]] = getReference(inst.operands[2]);
			if (names.find(inst.operands[2]) != names.end()) {
				Name n = names[inst.operands[2]];
				//out << t.name << " _" << inst.operands[1] << " = " << n.name << ";";
				//references[inst.operands[1]] = n.name;
			}
			else {
				//out << t.name << " _" << inst.operands[1] << " = _" << inst.operands[2] << ";";
				std::stringstream name;
				name << "_" << inst.operands[2];
				//references[inst.operands[1]] = name.str();
			}
			//out << " // OpLoad " << inst.operands[1] << ", " << inst.operands[2];
			break;
		}
		case OpStore: {
			output(out);
			Variable v = variables[inst.operands[0]];
			if (stage == EShLangFragment && v.storage == StorageClassOutput) {
				out << "gl_FragColor" << " = _" << inst.operands[1] << ";";
			}
			else {
				if (names.find(inst.operands[0]) != names.end()) {
					out << names[inst.operands[0]].name << " = " << getReference(inst.operands[1]) << ";";
				}
				else {
					out << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
				}
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
