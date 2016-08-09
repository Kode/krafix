#include "GlslTranslator.h"
#include <fstream>
#include <map>
#include <sstream>
#include <string.h>
#include "GlslFunctionStrings.h"

using namespace krafix;

typedef unsigned id;

namespace {
	std::vector<std::string> splitLines(std::string text) {
		std::vector<std::string> lines;
		unsigned lastSplit = 0;
		for (unsigned i = 0; i < text.size(); ++i) {
			if (text[i] == '\n') {
				lines.push_back(text.substr(lastSplit, i - lastSplit));
				lastSplit = i + 1;
			}
		}
		lines.push_back(text.substr(lastSplit));
		return lines;
	}
}

void GlslTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
	std::ofstream file;
	file.open(filename, std::ios::binary | std::ios::out);
	out = &file;
	
	if (stage != StageVertex && stage != StageFragment) {
		(*out) << "#version 400\n";
	}
	else {
		(*out) << "#version " << target.version << "\n";
		if (target.es && target.version >= 300) (*out) << " es\n";
	}

	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		outputInstruction(target, attributes, inst);
		if (outputting) (*out) << "\n";
	}
	for (unsigned i = 0; i < functions.size(); ++i) {
		if (functions[i]->name == "patch_main") {

		}
		else if (functions[i]->name == "main") {
			std::string patch;
			for (unsigned i2 = 0; i2 < functions.size(); ++i2) {
				if (functions[i2]->name == "patch_main") {
					patch = functions[i2]->text.str();
					break;
				}
			}
			if (patch.size() > 0) {
				std::vector<std::string> mainlines = splitLines(functions[i]->text.str());
				std::vector<std::string> patchlines = splitLines(patch);
				for (unsigned line = 0; line < 2; ++line) {
					(*out) << mainlines[line] << '\n';
				}
				for (unsigned line = 0; line < patchlines.size(); ++line) {
					if (patchlines[line].size() < 7 || patchlines[line].substr(patchlines[line].size() - 7) != "return;") (*out) << '\t' << patchlines[line] << '\n';
				}
				for (unsigned line = 2; line < mainlines.size(); ++line) {
					(*out) << mainlines[line] << '\n';
				}
			}
			else {
				(*out) << functions[i]->text.str();
			}
			(*out) << "\n\n";
		}
		else {
			(*out) << functions[i]->text.str();
			(*out) << "\n\n";
		}
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
				if (target.system == Android && stage == StageFragment) {
					(*out) << "#extension GL_OES_EGL_image_external : require\n";
				}
				if ((target.system == HTML5 || (target.es && target.version == 100)) && stage == StageFragment) {
					if (isFragDepthUsed) (*out) << "#extension GL_EXT_frag_depth : require\n";
					if (isFragDataUsed) (*out) << "#extension GL_EXT_draw_buffers : require\n";
					if (isTextureLodUsed) (*out) << "#extension GL_EXT_shader_texture_lod : require\n";
					if (isDerivativesUsed) (*out) << "#extension GL_OES_standard_derivatives : require\n";
				}

				for (std::map<unsigned, Type>::iterator it = types.begin(); it != types.end(); ++it) {
					Type& type = it->second;
					if (type.ispointer) continue;
					if (type.members.size() == 0) continue;
					if (strncmp(type.name.c_str(), "gl_", 3) == 0) continue;
					(*out) << "struct " << type.name << " {\n";
					for (std::map<unsigned, std::pair<std::string, Type>>::iterator it2 = type.members.begin(); it2 != type.members.end(); ++it2) {
						std::string& name = std::get<0>(it2->second);
						std::string type_name = std::get<1>(it2->second).name;
						(*out) << "\t" << type_name << " " << name << ";\n";
					}
					(*out) << "};\n";
				}

				if (target.es) {
					if (target.version >= 300) (*out) << "precision highp float;\n";
					else (*out) << "precision mediump float;\n";
				}

				if (target.version >= 300 && stage == StageFragment) {
					if (isFragDepthUsed) (*out) << "out float krafix_FragDepth;\n";
					else if (isFragDataUsed) (*out) << "out vec4 krafix_FragData[" << fragDataIndexIds.size() << "];\n";
					else (*out) << "out vec4 krafix_FragColor;\n";
				}

				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					unsigned id = v->first;
					Variable& variable = v->second;

					Type& t = types[variable.type];
					std::string name = getReference(id);

					if (variable.builtin) {
						continue;
					}

					switch (stage) {
					case StageVertex:
						if (variable.storage == StorageClassInput) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (target.version < 300) {
									(*out) << "attribute " << t.name << " " << name << ";\n";
								}
								else {
									(*out) << "in " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassOutput) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (target.version < 300) {
									(*out) << "varying " << t.name << " " << name << ";\n";
								}
								else {
									(*out) << "out " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassUniformConstant) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "uniform " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "uniform " << t.name << " " << name << ";\n";
								}
							}
						}
						else {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								(*out) << t.name << " " << name << ";\n";
							}
						}
						break;
					case StageFragment:
						if (variable.storage == StorageClassInput) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
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
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "uniform " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "uniform " << t.name << " " << name << ";\n";
								}
							}
						}
						else {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								(*out) << t.name << " " << name << ";\n";
							}
						}
						break;
					case StageGeometry:
					case StageTessControl:
					case StageTessEvaluation:
					case StageCompute:
						if (variable.storage == StorageClassInput) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "in " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "in " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassOutput) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "out " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "out " << t.name << " " << name << ";\n";
								}
							}
						}
						else if (variable.storage == StorageClassUniformConstant) {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								if (t.isarray) {
									(*out) << "uniform " << t.name << " " << name << "[" << t.length << "];\n";
								}
								else {
									(*out) << "uniform " << t.name << " " << name << ";\n";
								}
							}
						}
						else {
							if (strncmp(t.name.c_str(), "gl_", 3) != 0) {
								(*out) << t.name << " " << name << ";\n";
							}
						}
						break;
					}
				}
				(*out) << "\n";
				
				if (target.system == HTML5) {
					if (isTransposeUsed) (*out) << transposeFunctionString;
					if (isMatrixInverseUsed) (*out) << matrixInverseFunctionString;
					(*out) << "\n";
				}

				firstFunction = false;
			}

			if (funcName != "main" && funcName != "patch_main") {
				(*out) << funcType << " " << funcName << "(";
				for (unsigned i = 0; i < parameters.size(); ++i) {
					(*out) << parameters[i].type.name << " " << getReference(parameters[i].id);
					if (i < parameters.size() - 1) (*out) << ", ";
				}
				(*out) << ");\n";
			}

			startFunction(funcName);

			if (funcName == "patch_main") {
				(*out) << "if (gl_InvocationID == 0)\n";
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
	default:
		CStyleTranslator::outputInstruction(target, attributes, inst);
		break;
	}
}
