#include "JavaScriptTranslator.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <string.h>

using namespace krafix;

typedef unsigned id;

namespace {
#ifndef SYS_WINDOWS
	void _itoa(int value, char* str, int base) {
		sprintf(str, "%d", value);
	}
#endif

	std::string findfilename(const std::string& path) {
		size_t pos = 0;
		if (path.find_last_of('/') != std::string::npos) pos = std::max(pos, path.find_last_of('/') + 1);
		if (path.find_last_of('\\') != std::string::npos) pos = std::max(pos, path.find_last_of('\\') + 1);
		return path.substr(pos);
	}
}

void JavaScriptTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
	outputLine = 0;
	originalLine = -1;
	
	sourcemap->file = findfilename(filename);
	sourcemap->addSource(sourcefilename);
	
	std::string name = findfilename(sourcefilename);
	for (size_t i = 0; i < name.size(); ++i) {
		if (name[i] == '.') name[i] = '_';
		if (name[i] == '-') name[i] = '_';
	}
	name = name.substr(0, name.size() - 5);
	strcpy(this->name, name.c_str());
	
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
	
	(*out) << "//# sourceMappingURL=" << filename << ".map";
	
	file.close();
	
	char mapfilename[512];
	strcpy(mapfilename, filename);
	strcat(mapfilename, ".map");
	std::ofstream mapfile;
	mapfile.open(mapfilename, std::ios::binary | std::ios::out);
	mapfile << sourcemap->serialize();
	mapfile.close();
}

void JavaScriptTranslator::outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint) {
	id result = inst.operands[1];
	switch (entrypoint) {
		
		default:
			CStyleTranslator::outputLibraryInstruction(target, attributes, inst, entrypoint);
			break;
	}
}

void JavaScriptTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;
	
	switch (inst.opcode) {
		case OpLine: {
			unsigned line = inst.operands[2] - 1; // should be operands[1]?
			unsigned column = inst.operands[3] - 1;
			if (line != originalLine) {
				originalLine = line;
			}
			break;
		}
		case OpLabel: {
			if (firstLabel) {
				output(out); ++outputLine;
				if (firstFunction) {
					for (unsigned i = 0; i < variables.size(); ++i) {
						Variable variable = variables[i];
						
						Type& t = types[variable.type];
						Name n = names[variable.id];
						
						if (t.members.size() > 0) continue;
						if (n.name == "") continue;
						
						if (variable.storage != StorageClassUniformConstant
							&& variable.storage != StorageClassInput
							&& variable.storage != StorageClassOutput) {
							(*out) << "var " << t.name << " " << n.name << ";\n"; ++outputLine;
						}
					}
					(*out) << "\n"; ++outputLine;

					firstFunction = false;
				}
				
				startFunction(funcName);
				
				if (funcName == "main") {
					(*out) << "function " << name << "(input, output, vec2, vec3, vec4, mat4)\n"; ++outputLine;
				}
				else {
					(*out) << "function " << funcName << "(";
					for (unsigned i = 0; i < parameters.size(); ++i) {
						(*out) << parameters[i].type.name << " " << getReference(parameters[i].id);
						if (i < parameters.size() - 1) (*out) << ", ";
					}
					(*out) << ")\n"; ++outputLine;
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
		case OpVariable: {
			Type& resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			types[result] = resultType;
			Variable& v = variables[result];
			v.id = result;
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			v.declared = true;
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
				Type& t = types[v.type];
				if (t.isarray) {
					(*out) << t.name << " " << getReference(result) << "[" << t.length << "];\n"; ++outputLine;
				}
				else {
					(*out) << t.name << " " << getReference(result) << ";";
				}
			}
			break;
		}
		case OpCompositeConstruct: {
			Type& resultType = types[inst.operands[0]];
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
			Type& resultType = types[inst.operands[0]];
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
			Type& resultType = types[inst.operands[0]];
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
			Type& resultType = types[inst.operands[0]];
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
			Type& resultType = types[inst.operands[0]];
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
		case OpConvertSToF: {
			Type& resultType = types[inst.operands[0]];
			id result = inst.operands[1];
			id value = inst.operands[2];
			std::stringstream str;
			if (resultType.length > 1) {
				str << "float" << resultType.length << "(" << getReference(value) << ")";
			}
			else {
				str << "float(" << getReference(value) << ")";
			}
			references[result] = str.str();
			break;
		}
		case OpReturn:
			output(out); ++outputLine;
			(*out) << "return;";
			sourcemap->insert(outputLine, SourceMap::make_shared<SourceMap::ColMap>(0, 0, originalLine, 0));
			//printf("Mapping %i to %i.\n", originalLine, outputLine);
			break;
		case OpStore: {
			Variable& v = variables[inst.operands[0]];
			if (getReference(inst.operands[0]) == "param") {
				references[inst.operands[0]] = getReference(inst.operands[1]);
			}
			else {
				output(out); ++outputLine;
				if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
					(*out) << getReference(inst.operands[0]) << indexName(types[inst.operands[0]], compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
				}
				else if (stage == StageGeometry) {
					Variable& v = variables[inst.operands[0]];
					if (v.storage == StorageClassOutput || getReference(inst.operands[0]) == "gl_Position") {
						(*out) << "_output." << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
					}
					else {
						(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
					}
				}
				else {
					(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
				}
				
				sourcemap->insert(outputLine, SourceMap::make_shared<SourceMap::ColMap>(0, 0, originalLine, 0));
				//printf("Mapping %i to %i.\n", originalLine, outputLine);
			}
			break;
		}
		default:
			CStyleTranslator::outputInstruction(target, attributes, inst);
			break;
	}
}
