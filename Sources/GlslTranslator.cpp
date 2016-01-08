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
	case OpLabel: {
		if (firstLabel) {
			output(out);

			if (firstFunction) {
				if (target.system == Android && stage == EShLangFragment) {
					(*out) << "#extension GL_OES_EGL_image_external : require\n";
				}

				for (std::map<unsigned, Type>::iterator it = types.begin(); it != types.end(); ++it) {
					Type& type = it->second;
					if (type.ispointer) continue;
					if (type.members.size() == 0) continue;
					if (strncmp(type.name, "gl_", 3) == 0) continue;
					(*out) << "struct " << type.name << "{\n";
					for (std::map<unsigned, std::string>::iterator it2 = type.members.begin(); it2 != type.members.end(); ++it2) {
						std::string& name = it2->second;
						(*out) << "\t" << name << ";\n";
					}
					(*out) << "}\n";
				}

				if (target.version >= 300 && stage == EShLangFragment) {
					(*out) << "out vec4 krafix_FragColor;\n";
				}

				if (target.es) (*out) << "precision mediump float;\n";

				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					unsigned id = v->first;
					Variable& variable = v->second;

					Type t = types[variable.type];
					std::string name = getReference(id);

					if (variable.builtin) {
						continue;
					}

					switch (stage) {
					case EShLangVertex:
						if (variable.storage == StorageClassInput) {
							if (strncmp(t.name, "gl_", 3) != 0) {
								if (target.version < 300) {
									(*out) << "attribute " << t.name << " " << name << ";\n";
								}
								else {
									(*out) << "in " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassOutput) {
							if (strncmp(t.name, "gl_", 3) != 0) {
								if (target.version < 300) {
									(*out) << "varying " << t.name << " " << name << ";\n";
								}
								else {
									(*out) << "out " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassUniformConstant) {
							if (strncmp(t.name, "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "uniform " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "uniform " << t.name << " " << name << ";\n";
								}
							}
						}
						else {
							if (strncmp(t.name, "gl_", 3) != 0) {
								(*out) << t.name << " " << name << ";\n";
							}
						}
						break;
					case EShLangFragment:
						if (variable.storage == StorageClassInput) {
							if (strncmp(t.name, "gl_", 3) != 0) {
								if (target.version < 300) {
									(*out) << "varying " << t.name << " " << name << ";\n";
								}
								else {
									(*out) << "in " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassOutput) {

						}
						else if (variable.storage == StorageClassUniformConstant) {
							if (strncmp(t.name, "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "uniform " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "uniform " << t.name << " " << name << ";\n";
								}
							}
						}
						else {
							if (strncmp(t.name, "gl_", 3) != 0) {
								(*out) << t.name << " " << name << ";\n";
							}
						}
						break;
					case EShLangGeometry:
					case EShLangTessControl:
					case EShLangTessEvaluation:
						if (variable.storage == StorageClassInput) {
							(*out) << "in " << t.name << " " << name << ";\n";
						}
						else if (variable.storage == StorageClassOutput) {
							(*out) << "out " << t.name << " " << name << ";\n";
						}
						else if (variable.storage == StorageClassUniformConstant) {
							if (t.isarray) {
								(*out) << "uniform " << t.name << " " << name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "uniform " << t.name << " " << name << ";\n";
							}
						}
						else {
							(*out) << t.name << " " << name << ";\n";
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
	default:
		CStyleTranslator::outputInstruction(target, attributes, inst);
		break;
	}
}
