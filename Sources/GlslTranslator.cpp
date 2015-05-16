#include "GlslTranslator.h"
#include <fstream>
#include <map>
#include <sstream>
#include <string.h>

using namespace krafix;

typedef unsigned id;

void GlslTranslator::outputCode(const Target& target, const char* filename, std::map<std::string, int>& attributes) {
	std::ofstream file;
	file.open(filename, std::ios::binary | std::ios::out);
	out = &file;

	(*out) << "#version " << target.version << "\n";
	if (target.es) (*out) << "precision mediump float;\n";

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

void GlslTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {
	case OpExecutionMode: {
		output(out);
		switch (inst.operands[1]) {
		case ExecutionModeOutputVertices:
			(*out) << "layout(vertices = " << inst.operands[2] << ") out;";
			break;
		default:
			(*out) << "// Unknown execution mode";
		}
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
			}
			else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 3) {
				t.name = "vec3";
				t.length = 3;
			}
			else if (strcmp(subtype.name, "float") == 0 && inst.operands[2] == 4) {
				t.name = "vec4";
				t.length = 4;
			}
		}
		types[id] = t;
		break;
	}
	case OpTypeMatrix: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "mat?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name != NULL) {
			if (strcmp(subtype.name, "vec3") == 0 && inst.operands[2] == 3) {
				t.name = "mat3";
				t.length = 4;
				types[id] = t;
			}
			else if (strcmp(subtype.name, "vec4") == 0 && inst.operands[2] == 4) {
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
		if (video && target.system == Android) {
			t.name = "samplerExternalOES";
		}
		else {
			t.name = "sampler2D";
		}
		types[id] = t;
		break;
	}
	case OpVariable: {
		unsigned id = inst.operands[1];
		Variable& v = variables[id];
		v.type = inst.operands[0];
		v.storage = (StorageClass)inst.operands[2];
		v.declared = true; //v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(id) != names.end()) {
			if (target.version >= 300 && strcmp(names[id].name, "gl_FragColor") == 0) {
				names[id].name = "krafix_FragColor";
			}
			references[id] = names[id].name;
		}
		if (v.storage == StorageClassFunction && getReference(id) != "param") {
			output(out);
			(*out) << types[v.type].name << " " << getReference(id) << ";";
		}
		break;
	}
	case OpLabel: {
		if (firstLabel) {
			output(out);

			if (firstFunction) {
				if (target.system == Android && stage == EShLangFragment) {
					(*out) << "#extension GL_OES_EGL_image_external : require\n";
				}

				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					unsigned id = v->first;
					Variable& variable = v->second;

					Type t = types[variable.type];
					Name n = names[id];

					if (variable.builtin) {
						if (target.version >= 300 && strcmp(n.name, "krafix_FragColor") == 0) {
							(*out) << "out vec4 krafix_FragColor;\n";
						}
						else {
							continue;
						}
					}

					switch (stage) {
					case EShLangVertex:
						if (variable.storage == StorageClassInput) {
							if (target.version < 300) {
								(*out) << "attribute " << t.name << " " << n.name << ";\n";
							}
							else {
								(*out) << "in " << t.name << " " << n.name << ";\n";
							}
						}
						else if (variable.storage == StorageClassOutput) {
							if (target.version < 300) {
								(*out) << "varying " << t.name << " " << n.name << ";\n";
							}
							else {
								(*out) << "out " << t.name << " " << n.name << ";\n";
							}
						}
						else if (variable.storage == StorageClassUniformConstant) {
							(*out) << "uniform " << t.name << " " << n.name << ";\n";
						}
						break;
					case EShLangFragment:
						if (variable.storage == StorageClassInput) {
							if (target.version < 300) {
								(*out) << "varying " << t.name << " " << n.name << ";\n";
							}
							else {
								(*out) << "in " << t.name << " " << n.name << ";\n";
							}
						}
						else if (variable.storage == StorageClassUniformConstant) {
							(*out) << "uniform " << t.name << " " << n.name << ";\n";
						}
						break;
					case EShLangGeometry:
					case EShLangTessControl:
					case EShLangTessEvaluation:
						if (variable.storage == StorageClassInput) {
							(*out) << "in " << t.name << " " << n.name << ";\n";
						}
						else if (variable.storage == StorageClassOutput) {
							(*out) << "out " << t.name << " " << n.name << ";\n";
						}
						else if (variable.storage == StorageClassUniformConstant) {
							(*out) << "uniform " << t.name << " " << n.name << ";\n";
						}
						break;
					}
				}
				(*out) << "\n";
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

			(*out) << funcType << " " << funcName << "(";
			for (unsigned i = 0; i < parameters.size(); ++i) {
				(*out) << parameters[i].type.name << " " << getReference(parameters[i].id);
				if (i < parameters.size() - 1) (*out) << ", ";
			}
			(*out) << ")\n";
		
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
	case OpFunction: {
		firstLabel = true;
		parameters.clear();
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		if (result == entryPoint) {
			if (target.kore) {
				references[result] = "kore";
				funcName = "kore";
				funcType = "void";
			}
			else {
				references[result] = "main";
				funcName = "main";
				funcType = "void";
			}
		}
		else if (names.find(result) != names.end()) {
			std::string name = names[result].name;
			name = name.substr(0, name.find_first_of('('));
			references[result] = name;
			funcName = name;
			funcType = resultType.name;
		}
		break;
	}
	case OpCompositeConstruct: {
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
	case OpMatrixTimesVector: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id matrix = inst.operands[2];
		id vector = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(matrix) << " * " << getReference(vector) << ")";
		references[result] = str.str();
		break;
	}
	case OpTextureSample: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id sampler = inst.operands[2];
		id coordinate = inst.operands[3];
		std::stringstream str;
		if (target.version < 300) str << "texture2D";
		else str << "texture";
		str << "(" << getReference(sampler) << ", " << getReference(coordinate) << ")";
		references[result] = str.str();
		break;
	}
	case OpReturn:
		output(out);
		(*out) << "return;";
		break;
	case OpKill:
		output(out);
		(*out) << "discard;";
		break;
	case OpStore: {
		Variable& v = variables[inst.operands[0]];
		if (getReference(inst.operands[0]) == "param") {
			references[inst.operands[0]] = getReference(inst.operands[1]);
		}
		else if (stage == EShLangFragment && v.storage == StorageClassOutput && target.version < 300) {
			output(out);
			if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
				(*out) << "gl_FragColor." << indexName(compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
			}
			else {
				(*out) << "gl_FragColor" << " = " << getReference(inst.operands[1]) << ";";
			}
		}
		else {
			output(out);
			if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
				(*out) << getReference(inst.operands[0]) << "." << indexName(compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
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
