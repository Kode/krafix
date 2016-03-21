#include "MetalStageInTranslator.h"
#include <algorithm>

using namespace krafix;
using namespace spv;


void MetalVertexInStruct::padToOffset(unsigned offset) {
	if (offset > byteSize) {
		body << "\tchar pad" << padIndex++ << "[" << (offset - byteSize) << "];\n";
		byteSize = offset;
	}
}

void MetalStageInTranslator::outputCode(const Target& target,
										const MetalStageInTranslatorRenderContext& renderContext,
										std::ostream& output,
										std::map<std::string, int>& attributes) {
	out = &output;
	_pRenderContext = (MetalStageInTranslatorRenderContext*)&renderContext;

	tempNameIndex = bound;
	_nextMTLBufferIndex = 0;
	_nextMTLTextureIndex = 0;
	_nextMTLSamplerIndex = 0;
	_vertexInStructs.clear();

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
			v.id = id;
			v.type = inst.operands[0];
			v.storage = (StorageClass)inst.operands[2];
			v.declared = (v.storage == StorageClassInput ||
						  v.storage == StorageClassOutput ||
						  v.storage == StorageClassUniform ||
						  v.storage == StorageClassUniformConstant ||
						  v.storage == StorageClassPushConstant);

			std::string varName = getVariableName(id);
			Type& t = getBaseType(v.type);

			if (v.storage == StorageClassInput) {
				std::string vPfx;
				if ( !v.builtin ) {
					if (stage == EShLangVertex) {

						// Set attribute parameters of this variable
						const MetalVertexAttribute& vtxAttr = _pRenderContext->vertexAttributesByLocation[v.location];
						v.offset = vtxAttr.offset;
						v.stride = vtxAttr.stride;
						v.isPerInstance = vtxAttr.isPerInstance;
						v.binding = vtxAttr.binding;

						if (v.binding == _pRenderContext->vertexAttributeStageInBinding) {
							vPfx = "in.";
						} else {
							// Set the reference to use either vertex or instance index variable
							MetalVertexInStruct& inStruct = _vertexInStructs[v.binding];
							if (inStruct.name.empty()) { inStruct.name += "in" + std::to_string(v.binding); }
							vPfx = inStruct.name + "[" + getVariableName(v.isPerInstance ? instIdVarId : vtxIdVarId) + "].";
						}
					} else {
						vPfx = "in.";
					}
				}
				references[id] = vPfx + varName;
			}
			else if (v.storage == StorageClassOutput) {
				if (t.opcode != OpTypeStruct) {
					references[id] = std::string("out.") + varName;
				} else {
					references[id] = "out";
				}
			}
			else if (isUniformBufferMember(v, t)) {
				references[id] = std::string("uniforms.") + varName;
			} else {
				references[id] = varName;
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

			Type& resultType = types[inst.operands[0]];
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
				if (stage == EShLangVertex && _pRenderContext->shouldFlipVertexY) {
					output(out);
					(*out) << "out." << positionName << ".y = -out." << positionName << ".y;\t\t// Invert Y-axis for Metal\n";
				}
				output(out);
				(*out) << "return out;";
			}
			break;

		case OpCompositeConstruct: {
			Type& resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			types[result] = resultType;

			bool needsComma = false;
			std::stringstream tmpOut;
			tmpOut << resultType.name << "(";
			for (unsigned i = 2; i < inst.length; ++i) {
				needsComma = paramComma(&tmpOut, needsComma);
				tmpOut << getReference(inst.operands[i]);
			}
			tmpOut << ")";
			references[result] = outputTempVar(out, resultType.name, tmpOut.str());
			break;
		}

		case OpMatrixTimesMatrix: {
			Type& resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			types[result] = resultType;
			unsigned operand1 = inst.operands[2];
			unsigned operand2 = inst.operands[3];
			std::stringstream tmpOut;
			tmpOut << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
			references[result] = outputTempVar(out, resultType.name, tmpOut.str());
			break;
		}

		case OpMatrixTimesVector: {
			Type& resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			types[result] = resultType;
			unsigned matrix = inst.operands[2];
			unsigned vector = inst.operands[3];
			std::stringstream tmpOut;
			tmpOut << "(" << getReference(matrix) << " * " << getReference(vector) << ")";
			references[result] = outputTempVar(out, resultType.name, tmpOut.str());
			break;
		}

		case OpVectorTimesMatrix: {
			Type& resultType = types[inst.operands[0]];
			unsigned result = inst.operands[1];
			types[result] = resultType;
			unsigned vector = inst.operands[2];
			unsigned matrix = inst.operands[3];
			std::stringstream tmpOut;
			tmpOut << "(" << getReference(vector) << " * " << getReference(matrix) << ")";
			references[result] = outputTempVar(out, resultType.name, tmpOut.str());
			break;
		}

		case OpImageSampleImplicitLod:
		case OpImageSampleExplicitLod: {
			addSamplerReference(inst);
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
		outputVertexInStructs();
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
		needsComma = paramComma(out, needsComma);
		(*out) << funcName << "_in in [[stage_in]]";
	}

	for (auto iter = _vertexInStructs.begin(); iter != _vertexInStructs.end(); iter++) {
		unsigned binding = iter->first;
		MetalVertexInStruct& inStruct = iter->second;
		needsComma = paramComma(out, needsComma);
		const std::string& varName = inStruct.name;
		(*out) << "device " << funcName << "_" << varName << "* " << varName << " [[buffer(" << binding << ")]]";
	}

	if (_hasLooseUniforms) {
		needsComma = paramComma(out, needsComma);
		(*out) << "constant " << funcName << "_uniforms& uniforms [[buffer(0)]]";
	}

	// Remember what the resource indices at this point
	unsigned origMTLBufferIndex = _nextMTLBufferIndex;
	unsigned origMTLTextureIndex = _nextMTLTextureIndex;
	unsigned origMTLSamplerIndex = _nextMTLSamplerIndex;

	for (auto v = variables.begin(); v != variables.end(); ++v) {
		unsigned id = v->first;
		Variable& variable = v->second;

		Type& t = getBaseType(variable.type);
		std::string varName = getVariableName(id);

		if (variable.storage == StorageClassUniform ||
			variable.storage == StorageClassUniformConstant ||
			variable.storage == StorageClassPushConstant) {
			switch (t.opcode) {
				case OpTypeStruct:
					needsComma = paramComma(out, needsComma);
					(*out) << "constant " << t.name << "& " << varName << " [[buffer(" << getMetalResourceIndex(variable, OpTypeStruct) << ")]]";
					break;
				case OpTypeSampler:
					needsComma = paramComma(out, needsComma);
					(*out) << t.name << " " << varName << " [[sampler(" << getMetalResourceIndex(variable, OpTypeSampler) << ")]]";
					break;
				case OpTypeImage:
					needsComma = paramComma(out, needsComma);
					(*out) << t.name << "<float> " << varName << " [[texture(" << getMetalResourceIndex(variable, OpTypeImage) << ")]]";
					break;
				case OpTypeSampledImage:
					needsComma = paramComma(out, needsComma);
					(*out) << t.name << "<float> " << varName << " [[texture(" << getMetalResourceIndex(variable, OpTypeImage) << ")]]";
					(*out) << ", sampler " << varName << "Sampler [[sampler(" << getMetalResourceIndex(variable, OpTypeSampler) << ")]]";
					break;
				default:
					break;
			}
		}
		if (variable.storage == StorageClassInput && variable.builtin) {
			needsComma = paramComma(out, needsComma);
			(*out) << builtInTypeName(variable) << " " << varName << " [[" << builtInName(variable.builtinType) << "]]";
		}
	}

	// If this is the function declaration, restore the resource indices
	// so the same indices will be used for the function definition.
	if (asDeclaration) {
		_nextMTLBufferIndex = origMTLBufferIndex;
		_nextMTLTextureIndex = origMTLTextureIndex;
		_nextMTLSamplerIndex = origMTLSamplerIndex;
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
		needsComma = paramComma(out, needsComma);
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
	for (auto v = variables.begin(); v != variables.end(); ++v) {
		unsigned id = v->first;
		Variable& variable = v->second;

		Type& t = getBaseType(variable.type);

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
	for (auto v = variables.begin(); v != variables.end(); ++v) {
		Variable& variable = v->second;

		unsigned typeId = getBaseTypeID(variable.type);
		Type& t = types[typeId];

		if ((t.opcode == OpTypeStruct) && (variable.storage != StorageClassOutput)) {
			indent(out);
			(*out) << "struct " << t.name << " {\n";
			++indentation;
			for (unsigned mbrIdx = 0; mbrIdx < t.length; mbrIdx++) {
				unsigned mbrId = getMemberId(typeId, mbrIdx);
				Member member = members[mbrId];
				Type& mbrType = types[member.type];
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

/** Outputs the vertex attribute input structures, and adds them to the _vertexInStructs map. */
void MetalStageInTranslator::outputVertexInStructs() {
	if (stage != EShLangVertex) { return; }

	std::vector<std::pair<const unsigned, Variable>*> inVars;
	for (auto v = variables.begin(); v != variables.end(); ++v) {
		Variable& variable = v->second;
		if ((variable.storage == StorageClassInput) &&
			(variable.binding != _pRenderContext->vertexAttributeStageInBinding) &&
			!variable.builtin) {

			inVars.push_back(&(*v));
		}
	}
	unsigned varCnt = (unsigned)inVars.size();
	if (varCnt == 0) { return; }		// No input attributes to output

	std::sort (inVars.begin(), inVars.end(), compareByLocation);

	// Divide the input variables into separate structs according to their bindings.
	++indentation;
	for (unsigned varIdx = 0; varIdx < varCnt; varIdx++) {
		auto pVar = inVars[varIdx];
		unsigned id = pVar->first;
		Variable& variable = pVar->second;
		MetalVertexInStruct& inStruct = _vertexInStructs[variable.binding];
		if (inStruct.body.str().empty()) {
			inStruct.body  << "struct " << funcName << "_" << inStruct.name << " {\n";
		}

		// Add any padding required between the structure components
		if (variable.offset > inStruct.byteSize) {
			inStruct.body << "\tchar pad" << inStruct.padIndex++ << "[" << (variable.offset - inStruct.byteSize) << "];\n";
			inStruct.byteSize = variable.offset;
		}

		Type& t = getBaseType(variable.type);
		indent(&inStruct.body);
		if (t.opcode == OpTypeVector) { inStruct.body << "packed_"; }
		inStruct.body << t.name << " " << getVariableName(id) << ";\n";
		inStruct.byteSize += t.byteSize;
	}
	--indentation;

	// Finish and output the input variable structs.
	for (auto iter = _vertexInStructs.begin(); iter != _vertexInStructs.end(); iter++) {
		MetalVertexInStruct& inStruct = iter->second;
		inStruct.body << "};\n";
		(*out) << inStruct.body.str() << "\n";
	}
}

/**
 * If attribute inputs exist, collect them into a single stage-in structure, in location order,
 * and output the structure. Returns whether structure was output.
 */
bool MetalStageInTranslator::outputStageInStruct() {

	std::vector<std::pair<const unsigned, Variable>*> inVars;
	for (auto v = variables.begin(); v != variables.end(); ++v) {
		Variable& var = v->second;
		if ((var.storage == StorageClassInput) &&
			((stage == EShLangFragment) || (var.binding == _pRenderContext->vertexAttributeStageInBinding)) &&
			!var.builtin) {

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
		Variable& var = pVar->second;

		Type& t = types[var.type];

		indent(out);
		(*out) << t.name << " " << getVariableName(id);
		switch (stage) {
			case EShLangVertex:
				(*out) << " [[attribute("
				<< std::max(var.location, (signed)varIdx)	// Auto increment if locations were not specified
				<< ")]]";
				break;
			case EShLangFragment:
				if (var.location >= 0) { (*out) << " [[user(locn" << var.location << ")]]"; }
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
	for (auto v = variables.begin(); v != variables.end(); ++v) {
		unsigned id = v->first;
		Variable& var = v->second;

		if (var.storage == StorageClassOutput) {
			unsigned typeId = getBaseTypeID(var.type);
			Type& t = types[typeId];
			std::string varName = getVariableName(id);

			if (t.opcode == OpTypeStruct) {
				// Extract struct members and add them to stage-out struct
				for (unsigned mbrIdx = 0; mbrIdx < t.length; mbrIdx++) {
					unsigned mbrId = getMemberId(typeId, mbrIdx);
					Member member = members[mbrId];
					Type& mbrType = types[member.type];
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
								if (_pRenderContext->isRenderingPoints) {
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
				if (var.builtin) {
					switch (var.builtinType) {
						case BuiltInPosition:
							tmpOut << " [[" << builtInName(var.builtinType) << "]]";
							positionName = varName;
							break;
						case spv::BuiltInPointSize:
							if (_pRenderContext->isRenderingPoints) {
								tmpOut << " [[" << builtInName(var.builtinType) << "]]";
							}
							break;
						case BuiltInClipDistance:
							tmpOut << "  /* [[clip_distance]] built-in unsupported under Metal */";
							break;
						default:
							tmpOut << " [[" << builtInName(var.builtinType) << "]]";
							break;
					}
				} else {
					switch (stage) {
						case EShLangVertex:
							if (var.location >= 0) { tmpOut << " [[user(locn" << var.location << ")]]"; }
							break;
						default:
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

/** Builds and adds a reference for a sampler, based on the specified instruction. */
void MetalStageInTranslator::addSamplerReference(Instruction& inst) {
	unsigned result = inst.operands[1];
	unsigned sampler = inst.operands[2];
	unsigned coordinate = inst.operands[3];
	Type& sType = types[sampler];
	std::string tcRef = getReference(coordinate);
	if (_pRenderContext->shouldFlipFragmentY) {
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
	std::stringstream refStrm;
	refStrm << getReference(sampler) << ".sample(" << getReference(sampler) << "Sampler, " << tcRef;

	// Add any image sampling qualifiers derived from the image operands in the instruction
	std::string imgOpRef;
	ImageOperandsArray imageOperands;
	extractImageOperands(imageOperands, inst, 4);

	// Sampling bias
	imgOpRef = imageOperands[ImageOperandsBiasShift];
	if ( !imgOpRef.empty() ) { refStrm << ", bias(" << imgOpRef << ")"; }

	// Sampling LOD
	imgOpRef = imageOperands[ImageOperandsLodShift];
	if ( !imgOpRef.empty() ) { refStrm << ", level(" << imgOpRef << ")"; }

	// Sampling gradient
	imgOpRef = imageOperands[ImageOperandsGradShift];
	if ( !imgOpRef.empty() ) {
		switch (sType.imageDim) {
			case Dim2D:
				refStrm << ", gradient2d(" << imgOpRef << ")";
				break;
			case Dim3D:
				refStrm << ", gradient3d(" << imgOpRef << ")";
				break;
			case DimCube:
				refStrm << ", gradientcube(" << imgOpRef << ")";
				break;
			default:
				break;
		}
	}

	// Sampling offset
	imgOpRef = imageOperands[ImageOperandsOffsetShift];
	if ( !imgOpRef.empty() ) { refStrm << "," << imgOpRef; }

	// Sampling offset
	imgOpRef = imageOperands[ImageOperandsConstOffsetsShift];
	if ( !imgOpRef.empty() ) { refStrm << "," << imgOpRef; }

	refStrm << ")";
	references[result] = refStrm.str();
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
		case ExecutionModelKernel:					return EShLangVertex;
		default:                                    return EShLangVertex; // silence the compiler
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
bool MetalStageInTranslator::paramComma(std::ostream* out, bool needsComma) {
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


