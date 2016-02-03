
#include "MetalStageInTranslator.h"

using namespace krafix;
using namespace spv;

void MetalStageInTranslator::outputCode(const Target& target,
										const MetalStageInTranslatorRenderContext& renderContext,
										std::ostream* pOutput,
										std::map<std::string, int>& attributes) {
	out = pOutput;
	_renderContext = renderContext;

	_nextMTLBufferIndex = 0;
	_nextMTLTextureIndex = 0;
	_nextMTLSamplerIndex = 0;

	outputHeader();
	
	for (unsigned i = 0; i < instructions.size(); ++i) {
		outputting = false;
		Instruction& inst = instructions[i];
		outputInstruction(target, attributes, inst);
		if (outputting) { (*out) << "\n"; }
	}

	for (std::vector<Function*>::iterator iter = functions.begin(), end = functions.end(); iter != end; iter++) {
		(*out) << (*iter)->text.str();
		(*out) << "\n\n";
	}
}

void MetalStageInTranslator::outputHeader() {
	(*out) << "#include <metal_stdlib>\n";
	(*out) << "#include <simd/simd.h>\n";
	(*out) << "\n";
	(*out) << "using namespace metal;\n";
	(*out) << "\n";
}

void MetalStageInTranslator::outputInstruction(const Target& target,
										   std::map<std::string, int>& attributes,
										   Instruction& inst) {
	switch (inst.opcode) {

		case OpEntryPoint: {
			stage = stageFromSPIRVExecutionModel((ExecutionModel)inst.operands[0]);
			entryPoint = inst.operands[1];
			name = std::string(inst.string);
			name = cleanMSLFuncName(name);
			break;
		}

		case OpVariable: {
			unsigned id = inst.operands[1];
			Variable& v = variables[id];
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			v.declared = (v.storage == StorageClassInput ||
						  v.storage == StorageClassOutput ||
						  v.storage == StorageClassUniform ||
						  v.storage == StorageClassUniformConstant ||
						  v.storage == StorageClassPushConstant);

			std::string varName = getVariableName(id);
			Type t = types[v.type];
			if (t.opcode == OpTypePointer) { t = types[t.baseType]; }

			if (v.storage == StorageClassInput) {
				const char* vPfx = v.builtin ? "" : "in.";
				if (strcmp(t.name, "float2") == 0 || strcmp(t.name, "float3") == 0 || strcmp(t.name, "float4") == 0) {
					references[id] = std::string(t.name) + "(" + vPfx + varName + ")";
				} else {
					references[id] = std::string(vPfx) + varName;
				}
			}
			else if (v.storage == StorageClassOutput) {
				if (t.opcode != OpTypeStruct) {
					references[id] = std::string("out.") + varName;
				} else {
					references[id] = "out";
				}
			}
			else {
				if (isUniformBufferMember(v, t)) {
					references[id] = std::string("uniforms.") + varName;
				} else {
					references[id] = varName;
				}
			}
			break;
		}

		case OpFunction: {
			// Almost identical behaviour as CStyleTranslator.
			// Overriding to avoid MetalTranslator implementation.
			firstLabel = true;
			parameters.clear();

			unsigned result = inst.operands[1];
			_isEntryFunction = (result == entryPoint);
			funcName = cleanMSLFuncName(getFunctionName(result));
			references[result] = funcName;

			Type resultType = types[inst.operands[0]];
			types[result] = resultType;
			funcType = resultType.name;
			
			break;
		}

		case OpLabel: {
			if (firstLabel) {
				outputFunctionSignature(true);		// Output the function delaration
				startFunction(funcName);			// Defer output of function definition
				outputFunctionSignature(false);		// ...by record it into a Function text

				++indentation;
				if (_isEntryFunction && _hasStageOut) {
					output(out);
					(*out) << funcName << "_out out;";
				}
				firstLabel = false;
			} else {
				unsigned label = inst.operands[0];
				if (labelStarts.find(label) != labelStarts.end()) {
					if (labelStarts[label].at(0) == '}') {
						--indentation;
					}
					output(out);
					(*out) << labelStarts[label] << "\n";
					indent(out);
					(*out) << "{";
					++indentation;
				}
				else if (merges.find(label) != merges.end()) {
					--indentation;
					output(out);
					(*out) << "}";
				}
			}
			break;
		}

		case OpReturn:
			if (_isEntryFunction && _hasStageOut) {
				if (stage == EShLangVertex && _renderContext.shouldFlipVertexY) {
					output(out);
					(*out) << "out." << positionName << ".y = -out." << positionName << ".y;\t\t// Invert Y-axis for Metal\n";
				}
				output(out);
				(*out) << "return out;";
			}
			break;

		case OpImageSampleImplicitLod: {
			unsigned result = inst.operands[1];
			unsigned sampler = inst.operands[2];
			unsigned coordinate = inst.operands[3];
			Type& sType = types[sampler];
			std::stringstream str;
			std::string tcRef = getReference(coordinate);
			if (_renderContext.shouldFlipFragmentY) {
				switch (sType.imageDim) {
					case Dim2D:
						tcRef = "float2(" + tcRef + ".x, " + "(1.0 - " + tcRef + ".y))";
						break;
					case Dim3D:
					case DimCube:
						tcRef = "float3(" + tcRef + ".x, " + "(1.0 - " + tcRef + ".y), " + tcRef + ".z)";
						break;
					default:
						break;
				}
			}
			str << getReference(sampler) << ".sample(" << getReference(sampler) << "Sampler, " << tcRef << ")";
			references[result] = str.str();
			break;
		}

		default:
			MetalTranslator::outputInstruction(target, attributes, inst);
			break;
	}
}

/** Outputs a function signature. */
void MetalStageInTranslator::outputFunctionSignature(bool asDeclaration) {
	if (_isEntryFunction) {
		outputEntryFunctionSignature(asDeclaration);
	} else {
		outputLocalFunctionSignature(asDeclaration);
	}
}

/** Outputs the function signature of an entry function. */
void MetalStageInTranslator::outputEntryFunctionSignature(bool asDeclaration) {

	// If this is a declaration, output entry point function input and output variables
	if (asDeclaration) {
		_hasLooseUniforms = outputLooseUniformStruct();
		outputUniformBuffers();
		_hasStageIn = outputStageInStruct();
		_hasStageOut = outputStageOutStruct();
	}

	if (_hasStageOut) { funcType = funcName + "_out"; }

	// Entry functions need a type qualifier
	std::string entryType;
	switch (stage) {
		case EShLangVertex:
			entryType = "vertex";
			break;
		case EShLangFragment:
			entryType = executionModes.useEarlyFragmentTests ? "fragment [[ early_fragment_tests ]]" : "fragment";
			break;
		case EShLangCompute:
			entryType = "kernel";
			break;
		default:
			entryType = "unknown";
			break;
	}

	indent(out);
	(*out) << entryType << " " << funcType << " " << funcName << "(";

	bool needsComma = false;

	if (_hasStageIn) {
		needsComma = paramComma(needsComma);
		(*out) << funcName << "_in in [[stage_in]]";
	}

	if (_hasLooseUniforms) {
		needsComma = paramComma(needsComma);
		(*out) << "constant " << funcName << "_uniforms& uniforms [[buffer(0)]]";
	}

	for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
		unsigned id = v->first;
		Variable& variable = v->second;

		Type t = types[variable.type];
		if (t.opcode == OpTypePointer) { t = types[t.baseType]; }
		std::string varName = getVariableName(id);

		if (variable.storage == StorageClassUniform ||
			variable.storage == StorageClassUniformConstant ||
			variable.storage == StorageClassPushConstant) {
			switch (t.opcode) {
				case OpTypeStruct:
					needsComma = paramComma(needsComma);
					(*out) << "constant " << t.name << "& " << varName << " [[buffer(" << getMetalResourceIndex(variable, OpTypeStruct) << ")]]";
					break;
				case OpTypeSampler:
					needsComma = paramComma(needsComma);
					(*out) << t.name << " " << varName << " [[sampler(" << getMetalResourceIndex(variable, OpTypeSampler) << ")]]";
					break;
				case OpTypeImage:
					needsComma = paramComma(needsComma);
					(*out) << t.name << "<float> " << varName << " [[texture(" << getMetalResourceIndex(variable, OpTypeImage) << ")]]";
					break;
				case OpTypeSampledImage:
					needsComma = paramComma(needsComma);
					(*out) << t.name << "<float> " << varName << " [[texture(" << getMetalResourceIndex(variable, OpTypeImage) << ")]]";
					(*out) << ", sampler " << varName << "Sampler [[sampler(" << getMetalResourceIndex(variable, OpTypeSampler) << ")]]";
					break;
				default:
					break;
			}
		}
		if (variable.storage == StorageClassInput && variable.builtin) {
			needsComma = paramComma(needsComma);
			(*out) << builtInTypeName(variable.builtinType, t)
			<< " " << varName
			<< " [[" << builtInName(variable.builtinType) << "]]";
		}
	}

	outputFunctionParameters(asDeclaration, needsComma);
	closeFunctionSignature(asDeclaration);
}

/** Outputs the function signature of a local function. */
void MetalStageInTranslator::outputLocalFunctionSignature(bool asDeclaration) {
	indent(out);
	(*out) << funcType << " " << funcName << "(";
	outputFunctionParameters(asDeclaration, false);
	closeFunctionSignature(asDeclaration);
}

/** Outputs the function parameters for the current function. */
bool MetalStageInTranslator::outputFunctionParameters(bool asDeclaration, bool needsComma) {
	for (std::vector<Parameter>::iterator iter = parameters.begin(), end = parameters.end(); iter != end; iter++) {
		needsComma = paramComma(needsComma);
		(*out) << iter->type.name << " " << getReference(iter->id);
	}
	return needsComma;
}

void MetalStageInTranslator::closeFunctionSignature(bool asDeclaration) {
		(*out) << (asDeclaration ? ");\n" : ") {\n");
}

/**
 * If loose uniforms exist, collect them into a single structure and output it.
 * Returns whether structure was output.
 */
bool MetalStageInTranslator::outputLooseUniformStruct() {
	bool hasLooseUniforms = false;

	std::ostringstream tmpOut;
	indent(&tmpOut);
	tmpOut << "struct " << funcName << "_uniforms {\n";
	++indentation;
	for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
		unsigned id = v->first;
		Variable& variable = v->second;

		Type t = types[variable.type];
		if (t.opcode == OpTypePointer) { t = types[t.baseType]; }

		if (isUniformBufferMember(variable, t)) {
			tmpOut << t.name << " " << getVariableName(id);
			if (t.isarray) { tmpOut << "[" << t.length << "]"; }
			if (variable.builtin) { tmpOut << " [[" << builtInName(variable.builtinType) << "]]"; }
			tmpOut << ";\n";
			hasLooseUniforms = true;
		}
	}
	--indentation;
	indent(&tmpOut);
	tmpOut << "};\n\n";

	if (hasLooseUniforms) { (*out) << tmpOut.str(); }
	return hasLooseUniforms;
}

/** If named uniform buffers exist, output them. */
void MetalStageInTranslator::outputUniformBuffers() {
	for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
		Variable& variable = v->second;

		unsigned typeId = variable.type;
		Type t = types[typeId];
		if (t.opcode == OpTypePointer) {
			typeId = t.baseType;
			t = types[typeId];
		}

		if ((t.opcode == OpTypeStruct) && (variable.storage != StorageClassOutput)) {
			indent(out);
			(*out) << "struct " << t.name << " {\n";
			++indentation;
			for (unsigned mbrIdx = 0; mbrIdx < t.length; mbrIdx++) {
				unsigned mbrId = getMemberId(typeId, mbrIdx);
				Member member = members[mbrId];
				Type mbrType = types[member.type];
				indent(out);
				(*out) << mbrType.name << " " << member.name;
				if (mbrType.isarray) { (*out) << "[" << mbrType.length << "]"; }
				(*out) << ";\n";
			}
			--indentation;
			indent(out);
			(*out) << "};\n\n";
		}
	}
}
/**
 * If attribute inputs exist, collect them into a single stage-in structure, in location order,
 * and output the structure. Returns whether structure was output.
 */
bool MetalStageInTranslator::outputStageInStruct() {
	std::ostringstream tmpOut;

	std::vector<std::pair<const unsigned, Variable>*> inVars;
	for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
		Variable& variable = v->second;
		if (variable.storage == StorageClassInput && !variable.builtin) {
			inVars.push_back(&(*v));
		}
	}
	unsigned varCnt = (unsigned)inVars.size();
	if (varCnt == 0) { return false; }		// No stage-in attributes to output

	std::sort (inVars.begin(), inVars.end(), compareByLocation);
	(*out) << "struct " << funcName << "_in {\n";
	++indentation;
	for (unsigned varIdx = 0; varIdx < varCnt; varIdx++) {
		auto pVar = inVars[varIdx];
		unsigned id = pVar->first;
		Variable& variable = pVar->second;

		Type t = types[variable.type];

		indent(out);
		(*out) << t.name << " " << getVariableName(id);
		switch (stage) {
			case EShLangVertex:
				(*out) << " [[attribute("
				<< std::max(variable.location, varIdx)	// Auto increment if locations were not specified
				<< ")]]";
				break;
			default:
				break;
		}
		(*out) << ";\n";
	}
	--indentation;
	indent(out);
	(*out) << "};\n\n";

	return true;
}

/** Collect shader output into the output structure and output it. Returns whether output structure was output. */
bool MetalStageInTranslator::outputStageOutStruct() {
	bool hasStageOut = false;

	std::ostringstream tmpOut;
	indent(&tmpOut);
	tmpOut << "struct " << funcName << "_out {\n";
	++indentation;
	for (std::map<unsigned, Variable>::iterator v = variables.begin(); v != variables.end(); ++v) {
		unsigned id = v->first;
		Variable& variable = v->second;

		if (variable.storage == StorageClassOutput) {
			unsigned typeId = variable.type;
			Type t = types[typeId];
			if (t.opcode == OpTypePointer) {
				typeId = t.baseType;
				t = types[typeId];
			}
			std::string varName = getVariableName(id);

			if (t.opcode == OpTypeStruct) {
				// Extract struct members and add them to stage-out struct
				for (unsigned mbrIdx = 0; mbrIdx < t.length; mbrIdx++) {
					unsigned mbrId = getMemberId(typeId, mbrIdx);
					Member member = members[mbrId];
					Type mbrType = types[member.type];
					indent(&tmpOut);
					tmpOut << mbrType.name << " " << member.name;
					if (mbrType.isarray) { tmpOut << "[" << mbrType.length << "]"; }
					if (member.builtin) {
						switch (member.builtinType) {
							case BuiltInPosition:
								tmpOut << " [[" << builtInName(member.builtinType) << "]]";
								positionName = member.name;
								break;
							case spv::BuiltInPointSize:
								if (_renderContext.isRenderingPoints) {
									tmpOut << " [[" << builtInName(member.builtinType) << "]]";
								}
								break;
							case BuiltInClipDistance:
								tmpOut << "  /* [[clip_distance]] built-in unsupported under Metal */";
								break;
							default:
								tmpOut << " [[" << builtInName(member.builtinType) << "]]";
								break;
						}
					}
					tmpOut << ";\n";
					hasStageOut = true;
				}
			} else {
				indent(&tmpOut);
				tmpOut << t.name << " " << varName;
				if (t.isarray) { tmpOut << "[" << t.length << "]"; }
				if (variable.builtin) {
					switch (variable.builtinType) {
						case BuiltInPosition:
							tmpOut << " [[" << builtInName(variable.builtinType) << "]]";
							positionName = varName;
							break;
						case spv::BuiltInPointSize:
							if (_renderContext.isRenderingPoints) {
								tmpOut << " [[" << builtInName(variable.builtinType) << "]]";
							}
							break;
						case BuiltInClipDistance:
							tmpOut << "  /* [[clip_distance]] built-in unsupported under Metal */";
							break;
						default:
							tmpOut << " [[" << builtInName(variable.builtinType) << "]]";
							break;
					}
				}
				tmpOut << ";\n";
				hasStageOut = true;
			}
		}
	}
	--indentation;
	indent(&tmpOut);
	tmpOut << "};\n\n";

	if (hasStageOut) { (*out) << tmpOut.str(); }
	return hasStageOut;
}

/** Returns the shader stage corresponding to the specified SPIRV execution model. */
EShLanguage MetalStageInTranslator::stageFromSPIRVExecutionModel(ExecutionModel execModel) {
	switch (execModel) {
		case ExecutionModelVertex:					return EShLangVertex;
		case ExecutionModelTessellationControl:		return EShLangTessControl;
		case ExecutionModelTessellationEvaluation:	return EShLangTessEvaluation;
		case ExecutionModelGeometry:				return EShLangGeometry;
		case ExecutionModelFragment:				return EShLangFragment;
		case ExecutionModelGLCompute:				return EShLangCompute;
		case ExecutionModelKernel:					return EShLangCompute;
	}
}

/** 
 * Returns the Metal index of the resource of the specified type as used by the specified variable.
 *
 * This implementation simply increments the value of each type of Metal resource index on
 * each request. This function can be overridden by subclasses to provide a different mechanism
 * for associating shader variables with Metal resource indexes, such as mapping the descriptor
 * set binding of the variable to a specific Metal resource index.
 */
signed MetalStageInTranslator::getMetalResourceIndex(Variable& variable, spv::Op rezType) {
	switch (rezType) {
		case OpTypeStruct:	return _nextMTLBufferIndex++;
		case OpTypeImage:	return _nextMTLTextureIndex++;
		case OpTypeSampler:	return _nextMTLSamplerIndex++;
		default:			return 0;
	}
}

bool MetalStageInTranslator::isUniformBufferMember(Variable& var, Type& type) {
	using namespace spv;

	if (var.storage != StorageClassUniformConstant) { return false; }

	switch (type.opcode) {
		case OpTypeBool:
		case OpTypeInt:
		case OpTypeFloat:
		case OpTypeVector:
		case OpTypeMatrix:
		case OpTypeStruct:
			return true;
		default:
			return false;
	}
}

/** Outputs a parameter-separating comma if needed, and returns the need for further commas. */
bool MetalStageInTranslator::paramComma(bool needsComma) {
	if (needsComma) { (*out) << ", "; }
	return true;
}


std::string& krafix::cleanMSLFuncName(std::string& funcName) {
	static std::string _cleanMainFuncName = "mmain";
	return (funcName == "main") ? _cleanMainFuncName : funcName;
}

bool krafix::compareByLocation(KrafixVarPair* vp1, KrafixVarPair* vp2) {
	Variable& v1 = vp1->second;
	Variable& v2 = vp2->second;
	if ( !v1.builtin && v2.builtin) { return true; }
	return v1.location < v2.location;
}


