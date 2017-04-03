#include "HlslTranslator.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <string.h>

using namespace krafix;

typedef unsigned id;

namespace {
#ifndef _WIN32
	void _itoa(int value, char* str, int base) {
		sprintf(str, "%d", value);
	}
#endif

	std::string positionName = "gl_Position";
	std::map<unsigned, Name> currentNames;

	unsigned localSizeX = 1;
	unsigned localSizeY = 1;
	unsigned localSizeZ = 1;

	bool compareVariables(const Variable& v1, const Variable& v2) {
		Name n1 = currentNames[v1.id];
		Name n2 = currentNames[v2.id];
		return strcmp(n1.name.c_str(), n2.name.c_str()) < 0;
	}
}

void HlslTranslator::outputCode(const Target& target, const char* sourcefilename, const char* filename, std::map<std::string, int>& attributes) {
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
	case GLSLstd450FMix: {
		std::stringstream str;
		id x = inst.operands[4];
		id y = inst.operands[5];
		id a = inst.operands[6];
		str << "lerp(" << getReference(x) << ", " << getReference(y) << ", " << getReference(a) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Atan2: {
		id y = inst.operands[4];
		id x = inst.operands[5];
		std::stringstream str;
		str << "atan2(" << getReference(y) << ", " << getReference(x) << ")";
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
				if (target.system != Unity) {
					(*out) << "float mod(float x, float y) {\n";
					(*out) << "\treturn x - y * floor(x / y);\n";
					(*out) << "}\n\n";
				}

				if (stage == StageVertex && target.version == 9) {
					(*out) << "uniform float4 dx_ViewAdjust;";
				}
				(*out) << "\n";

				std::vector<Variable> sortedVariables;
				for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
					//if (strncmp(types[v->second.type].name, "gl_", 3) == 0) continue;
					sortedVariables.push_back(v->second);
				}
				currentNames = names;
				std::sort(sortedVariables.begin(), sortedVariables.end(), compareVariables);

				if (stage == StageVertex) {
					indent(out);
					(*out) << "static float4 v_gl_Position;\n";
				}
				else if (stage == StageTessEvaluation) {
					indent(out);
					(*out) << "static float4 gl_Position;\n";
				}

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type& t = types[variable.type];
					Name n = names[variable.id];

					if (t.members.size() > 0) continue;
					if (stage == StageVertex && n.name.substr(0, 3) == "gl_") continue;

					if (n.name == "") continue;

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
						if (stage == StageVertex) {
							if (t.isarray) {
								(*out) << "static " << t.name << " v_" << n.name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "static " << t.name << " v_" << n.name << ";\n";
							}
						}
						else if (stage == StageTessControl) {
							if (t.isarray) {
								(*out) << "static " << t.name << " tc_" << n.name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "static " << t.name << " tc_" << n.name << ";\n";
							}
						}
						else if (stage == StageTessEvaluation) {
							if (t.isarray) {
								(*out) << "static " << t.name << " te_" << n.name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "static " << t.name << " te_" << n.name << ";\n";
							}
						}
						else if (stage == StageGeometry) {
							if (n.name == "gl_in") {
								(*out) << "static float4 gl_Position" << "[" << t.length << "];\n";
							}
							else if (t.isarray) {
								(*out) << "static " << t.name << " g_" << n.name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "static " << t.name << " g_" << n.name << ";\n";
							}
						}
						else if (stage == StageCompute) {
							if (t.isarray) {
								(*out) << "static " << t.name << " c_" << n.name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "static " << t.name << " c_" << n.name << ";\n";
							}
						}
						else {
							if (t.isarray) {
								(*out) << "static " << t.name << " f_" << n.name << "[" << t.length << "];\n";
							}
							else {
								(*out) << "static " << t.name << " f_" << n.name << ";\n";
							}
						}
					}
				}
				(*out) << "\n";

				if (stage == StageFragment) {
					(*out) << "struct InputFrag {\n";
				}
				else if (stage == StageTessControl) {
					(*out) << "struct InputTessC {\n";
				}
				else if (stage == StageTessEvaluation) {
					(*out) << "struct InputTessE {\n";
				}
				else if (stage == StageGeometry) {
					(*out) << "struct InputGeom {\n";
				}
				else {
					(*out) << "struct InputVert {\n";
				}
				++indentation;
				if ((stage == StageFragment || stage ==StageTessControl) && target.version > 9) {
					indent(out);
					(*out) << "float4 gl_Position : SV_POSITION;\n";
				}
				else if ((stage == StageFragment && target.version == 9) || target.system == Unity) {
					indent(out);
					(*out) << "float4 gl_Position : POSITION;\n";
				}
				else if (stage == StageGeometry) {
					indent(out);
					(*out) << "float4 gl_Position : POSITION;\n";
				}
				int uvindex = 0;
				int threeindex = 0;
				int index = 0;
				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type& t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassInput && strncmp(n.name.c_str(), "gl_", 3) != 0) {
						indent(out);
						if (stage == StageVertex && target.system == Unity) {
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
							if (t.name == "float4x4" && stage == StageVertex) {
								for (int i = 0; i < 4; ++i) {
									char name[101];
									strcpy(name, n.name.c_str());
									strcat(name, "_");
									size_t length = strlen(name);
									_itoa(i, &name[length], 10);
									name[length + 1] = 0;
									(*out) << "float4 " << name << " : TEXCOORD" << index << ";\n";
									if (stage == StageVertex) {
										attributes[name] = index;
									}
									if (i != 3) indent(out);
									++index;
								}
								--index;
							}
							else {
								(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
								if (stage == StageVertex) {
									attributes[n.name] = index;
								}
							}
						}
						++index;
					}
				}
				--indentation;
				indent(out);
				(*out) << "};\n\n";

				indent(out);
				if (stage == StageFragment) {
					(*out) << "struct OutputFrag {\n";
				}
				else if (stage == StageTessControl) {
					(*out) << "struct OutputTessC {\n";
				}
				else if (stage == StageTessEvaluation) {
					(*out) << "struct OutputTessE {\n";
				}
				else if (stage == StageGeometry) {
					(*out) << "struct OutputGeom {\n";
				}
				else {
					(*out) << "struct OutputVert {\n";
				}
				++indentation;
				if (stage == StageVertex && target.version > 9) {
					indent(out);
					(*out) << "float4 gl_Position : SV_POSITION;\n";
				}
				else if ((stage == StageVertex && target.version == 9) || target.system == Unity) {
					indent(out);
					(*out) << "float4 gl_Position : POSITION;\n";
				}
				else if (stage == StageGeometry) {
					indent(out);
					(*out) << "float4 gl_Position : POSITION;\n";
				}
				else if (stage == StageTessEvaluation) {
					indent(out);
					(*out) << "float4 gl_Position : SV_POSITION;\n";
				}
				index = 0;

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type& t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassOutput) {
						/*if (variable.builtin && stage == StageVertex) {
							positionName = n.name;
							indent(out);
							if (target.version == 9) {
								(*out) << t.name << " " << n.name << " : POSITION;\n";
							}
							else {
								(*out) << t.name << " " << n.name << " : SV_POSITION;\n";
							}
						}
						else*/ if (stage == StageFragment) {
							indent(out);
							(*out) << t.name << " " << n.name << " : COLOR;\n";
						}
					}
				}

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type& t = types[variable.type];
					Name n = names[variable.id];

					if (variable.storage == StorageClassOutput) {
						if (t.members.size() > 0) {

						}
						else if (variable.builtin && stage == StageVertex) {

						}
						else if (stage == StageFragment) {

						}
						else if (stage == StageTessControl) {
							if (n.name.substr(0, 3) == "gl_") continue;
							indent(out);
							(*out) << t.name << " " << n.name << " : TEXCOORD" << index << ";\n";
							++index;
						}
						else if (stage == StageGeometry) {
							indent(out);
							(*out) << t.name << " g_" << n.name << " : TEXCOORD" << index << ";\n";
							++index;
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

				if (stage == StageFragment) {
					(*out) << "void frag_main();\n\n";
				}
				else if (stage == StageTessControl) {
					(*out) << "void tesc_main();\n\n";
				}
				else if (stage == StageTessEvaluation) {
					(*out) << "void tese_main();\n\n";
				}
				else if (stage == StageGeometry) {
					(*out) << "static OutputGeom _output;\n";
					indent(out);
					(*out) << "void geom_main(inout TriangleStream<OutputGeom> _output_stream);\n\n";
				}
				else if (stage == StageCompute) {
					(*out) << "void comp_main();\n\n";
				}
				else {
					(*out) << "void vert_main();\n\n";
				}

				if (target.system == Unity) {
					if (stage == StageFragment) {
						(*out) << "OutputFrag frag(InputFrag input)\n";
					}
					else {
						(*out) << "OutputVert vert(InputVert input)\n";
					}
				}
				else {
					if (stage == StageFragment) {
						(*out) << "OutputFrag main(InputFrag input)\n";
					}
					else if (stage == StageTessControl) {
						(*out) << "[domain(\"tri\")]\n"; indent(out);
						(*out) << "[partitioning(\"integer\")]\n"; indent(out);
						(*out) << "[outputtopology(\"triangle_cw\")]\n"; indent(out);
						(*out) << "[outputcontrolpoints(3)]\n"; indent(out);
						(*out) << "[patchconstantfunc(\"patch\")]\n"; indent(out);
						(*out) << "OutputTessC main(InputPatch<InputTessC, 3> input, uint pointId : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)\n";
					}
					else if (stage == StageTessEvaluation) {
						(*out) << "[domain(\"tri\")]\n"; indent(out);
						(*out) << "OutputTessE main(float inside : SV_InsideTessFactor, float edges[3] : SV_TessFactor, float3 gl_TessCoord : SV_DomainLocation, const OutputPatch<InputTessE, 3> input)\n";
					}
					else if (stage == StageGeometry) {
						(*out) << "[maxvertexcount(3)]\n"; indent(out);
						(*out) << "void main(triangle InputGeom input[3], inout TriangleStream<OutputGeom> _output_stream)\n";
					}
					else if (stage == StageCompute) {
						(*out) << "[numthreads(" << localSizeX << ", " << localSizeY << ", " << localSizeZ <<")]\n"; indent(out);
						(*out) << "void main(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID)\n";
					}
					else {
						(*out) << "OutputVert main(InputVert input)\n";
					}
				}

				indent(out);
				(*out) << "{\n";
				++indentation;

				if (stage == StageTessEvaluation) {
					indent(out); (*out) << "te_gl_TessCoord = gl_TessCoord;\n";
				}
				else if (stage == StageTessControl) {
					indent(out); (*out) << "tc_gl_InvocationID = 0;\n";
				}
				else if (stage == StageCompute) {
					indent(out); (*out) << "c_gl_WorkGroupID = groupID;\n";
					indent(out); (*out) << "c_gl_LocalInvocationID = groupThreadID;\n";
					indent(out); (*out) << "c_gl_GlobalInvocationID = dispatchThreadID;\n";
				}

				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type& t = types[variable.type];
					Name n = names[variable.id];

					if (t.members.size() > 0) continue;
					if (n.name.substr(0, 3) == "gl_") continue;

					if (variable.storage == StorageClassInput) {
						indent(out);
						if (stage == StageVertex) {
							if (t.name == "float4x4") {
								(*out) << "v_" << n.name << "[0] = input." << n.name << "_0;\n"; indent(out);
								(*out) << "v_" << n.name << "[1] = input." << n.name << "_1;\n"; indent(out);
								(*out) << "v_" << n.name << "[2] = input." << n.name << "_2;\n"; indent(out);
								(*out) << "v_" << n.name << "[3] = input." << n.name << "_3;\n";
							}
							else {
								(*out) << "v_" << n.name << " = input." << n.name << ";\n";
							}
						}
						else if (stage == StageTessControl) {
							(*out) << "tc_" << n.name << "[tc_gl_InvocationID] = input[patchId]." << n.name << ";\n";
						}
						else if (stage == StageTessEvaluation) {
							(*out) << "te_" << n.name << "[0] = input[0]." << n.name << ";\n"; indent(out);
							(*out) << "te_" << n.name << "[1] = input[1]." << n.name << ";\n"; indent(out);
							(*out) << "te_" << n.name << "[2] = input[2]." << n.name << ";\n";
						}
						else if (stage == StageGeometry) {
							(*out) << "g_" << n.name << "[0] = input[0]." << n.name << ";\n"; indent(out);
							(*out) << "g_" << n.name << "[1] = input[1]." << n.name << ";\n"; indent(out);
							(*out) << "g_" << n.name << "[2] = input[2]." << n.name << ";\n";
						}
						else {
							(*out) << "f_" << n.name << " = input." << n.name << ";\n";
						}
					}
				}

				indent(out);
				if (stage == StageFragment) {
					(*out) << "frag_main();\n";
				}
				else if (stage == StageTessControl) {
					(*out) << "tesc_main();\n";
				}
				else if (stage == StageTessEvaluation) {
					(*out) << "tese_main();\n";
				}
				else if (stage == StageGeometry) {
					(*out) << "geom_main(_output_stream);\n";
				}
				else if (stage == StageCompute) {
					(*out) << "comp_main();\n";
				}
				else {
					(*out) << "vert_main();\n";
				}
				indent(out);
				if (stage == StageFragment) {
					(*out) << "OutputFrag output;\n";
				}
				else if (stage == StageTessControl) {
					(*out) << "OutputTessC output;\n";
				}
				else if (stage == StageTessEvaluation) {
					(*out) << "OutputTessE output;\n";
				}
				else if (stage == StageGeometry) {
					(*out) << "\n";
				}
				else if (stage == StageCompute) {
					(*out) << "\n";
				}
				else {
					(*out) << "OutputVert output;\n";
				}

				if (stage == StageVertex) {
					indent(out);
					(*out) << "output.gl_Position = v_gl_Position;\n";
				}
				if (stage == StageTessEvaluation) {
					indent(out);
					(*out) << "output.gl_Position = gl_Position;\n";
				}
				for (unsigned i = 0; i < sortedVariables.size(); ++i) {
					Variable variable = sortedVariables[i];

					Type& t = types[variable.type];
					Name n = names[variable.id];

					if (t.members.size() > 0) continue;

					if (variable.storage == StorageClassOutput) {
						indent(out);
						if (stage == StageVertex) {
							(*out) << "output." << n.name << " = v_" << n.name << ";\n";
						}
						else if (stage == StageTessControl) {
							if (n.name.substr(0, 3) == "gl_") {
								(*out) << "\n";
								continue;
							}
							(*out) << "output." << n.name << " = tc_" << n.name << "[tc_gl_InvocationID];\n";
						}
						else if (stage == StageTessEvaluation) {
							(*out) << "output." << n.name << " = te_" << n.name << ";\n";
						}
						else if (stage == StageGeometry) {
							(*out) << "\n";
						}
						else {
							(*out) << "output." << n.name << " = f_" << n.name << ";\n";
						}
					}
				}

				indent(out);
				if (stage == StageVertex) {
					if (target.version == 9 && target.system != Unity) {
						(*out) << "output." << positionName << ".x = output." << positionName << ".x - dx_ViewAdjust.x * output." << positionName << ".w;\n";
						indent(out);
						(*out) << "output." << positionName << ".y = output." << positionName << ".y + dx_ViewAdjust.y * output." << positionName << ".w;\n";
						indent(out);
					}
					(*out) << "output." << positionName << ".z = (output." << positionName << ".z + output." << positionName << ".w) * 0.5;\n";
					indent(out);
				}
				
				if (stage == StageGeometry || stage == StageCompute) {
					(*out) << "\n";
				}
				else {
					(*out) << "return output;\n";
				}

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
				if (stage == StageFragment) {
					(*out) << "void frag_main()\n";
				}
				else if (stage == StageGeometry) {
					(*out) << "void geom_main(inout TriangleStream<OutputGeom> _output_stream)\n";
				}
				else if (stage == StageTessEvaluation) {
					(*out) << "void tese_main()\n";
				}
				else if (stage == StageTessControl) {
					(*out) << "struct PatchOutputTessC {\n"; ++indentation; indent(out);
					(*out) << "float gl_TessLevelInner : SV_InsideTessFactor;\n"; indent(out);
					(*out) << "float gl_TessLevelOuter[3] : SV_TessFactor;\n"; --indentation; indent(out);
					(*out) << "};\n\n"; indent(out);

					(*out) << "PatchOutputTessC patch() {\n"; ++indentation; indent(out);
					(*out) << "PatchOutputTessC output;\n"; indent(out);
					(*out) << "patch_main();\n"; indent(out);
					(*out) << "output.gl_TessLevelInner = tc_gl_TessLevelInner[0];\n"; indent(out);
					(*out) << "output.gl_TessLevelOuter[0] = tc_gl_TessLevelOuter[0];\n"; indent(out);
					(*out) << "output.gl_TessLevelOuter[1] = tc_gl_TessLevelOuter[1];\n"; indent(out);
					(*out) << "output.gl_TessLevelOuter[2] = tc_gl_TessLevelOuter[2];\n"; indent(out);
					(*out) << "return output;\n"; --indentation; indent(out);
					(*out) << "}\n\n"; indent(out);

					(*out) << "void tesc_main()\n";
				}
				else if (stage == StageCompute) {
					(*out) << "void comp_main()\n";
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
	case OpExecutionMode: {
		unsigned mode = inst.operands[1];
		switch (mode) {
		case ExecutionModeLocalSize:
			localSizeX = inst.operands[2];
			localSizeY = inst.operands[3];
			localSizeZ = inst.operands[4];
			break;
		}
		break;
	}
	case OpTypeArray: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.isarray = true;
		t.name = "unknownarray";
		Type& subtype = types[inst.operands[1]];
		t.length = atoi(references[inst.operands[2]].c_str());
		if (subtype.name == "float") {
			t.name = "float";
		}
		else if (subtype.name == "float2") {
			t.name = "float2";
		}
		else if (subtype.name == "float3") {
			t.name = "float3";
		}
		else if (subtype.name == "float4") {
			t.name = "float4";
		}
		break;
	}
	case OpTypeVector: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.name = "float?";
		Type& subtype = types[inst.operands[1]];
		if (subtype.name == "int" && inst.operands[2] == 2) {
			t.name = "int2";
			t.length = 2;
		}
		else if (subtype.name == "int" && inst.operands[2] == 3) {
			t.name = "int3";
			t.length = 3;
		}
		else if (subtype.name == "int" && inst.operands[2] == 4) {
			t.name = "int4";
			t.length = 4;
		}
		else if (subtype.name == "float" && inst.operands[2] == 2) {
			t.name = "float2";
			t.length = 2;
		}
		else if (subtype.name == "float" && inst.operands[2] == 3) {
			t.name = "float3";
			t.length = 3;
		}
		else if (subtype.name == "float" && inst.operands[2] == 4) {
			t.name = "float4";
			t.length = 4;
		}
		break;
	}
	case OpTypeMatrix: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.name = "float4x?";
		Type& subtype = types[inst.operands[1]];
		if (subtype.name == "float2" && inst.operands[2] == 3) {
			t.name = "float2x2";
			t.length = 4;
		}
		if (subtype.name == "float3" && inst.operands[2] == 3) {
			t.name = "float3x3";
			t.length = 4;
		}
		if (subtype.name == "float4" && inst.operands[2] == 4) {
			t.name = "float4x4";
			t.length = 4;
		}
		break;
	}
	case OpTypeImage: {
		Type t;
		unsigned id = inst.operands[0];
		if (stage == StageCompute) {
			t.name = "RWTexture2D<float4>";
		}
		else {
			t.name = "sampler2D";
		}
		types[id] = t;
		break;
	}
	case OpImageWrite: {
		id image = inst.operands[0];
		id coordinate = inst.operands[1];
		id texels = inst.operands[2];
		output(out);
		(*out) << getReference(image) << "[" << getReference(coordinate) << "] = " << getReference(texels) << ";\n";
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
		v.declared = true; // v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(result) != names.end()) {
			if (v.storage == StorageClassInput || v.storage == StorageClassOutput) {
				if (stage == StageVertex) {
					references[result] = std::string("v_") + names[result].name;
				}
				else if (stage == StageTessControl) {
					references[result] = std::string("tc_") + names[result].name;
				}
				else if (stage == StageTessEvaluation) {
					references[result] = std::string("te_") + names[result].name;
				}
				else if (stage == StageGeometry) {
					references[result] = std::string("g_") + names[result].name;
				}
				else if (stage == StageCompute) {
					references[result] = std::string("c_") + names[result].name;
				}
				else {
					references[result] = std::string("f_") + names[result].name;
				}
			}
			else {
				references[result] = names[result].name;
			}
		}
		if (v.storage == StorageClassFunction && getReference(result) != "param") {
			output(out);
			Type& t = types[v.type];
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
	case OpEmitVertex:
		output(out);
		(*out) << "_output_stream.Append(_output);";
		break;
	case OpEndPrimitive:
		output(out);
		(*out) << "//_output_stream.RestartStrip();";
		break;
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
		}
		break;
	}
	default:
		CStyleTranslator::outputInstruction(target, attributes, inst);
		break;
	}
}
