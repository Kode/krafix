
#pragma once

#include "MetalTranslator.h"

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <sstream>


namespace krafix {

	/** Additional info about vertex attributes. */
	struct MetalVertexAttribute {
		unsigned binding;
		unsigned offset;
		unsigned stride;
		bool isPerInstance;

		MetalVertexAttribute() : binding(0), offset(0), stride(0), isPerInstance(false) {}
	};

	struct MetalVertexInStruct {
		std::string name;
		std::ostringstream body;
		unsigned byteSize;
		unsigned padIndex;

		void padToOffset(unsigned offset);

		MetalVertexInStruct() : byteSize(0), padIndex(0) {}
	};

	/** The rendering context in which a shader conversion occurs. */
	struct MetalStageInTranslatorRenderContext {
		std::unordered_map<unsigned, MetalVertexAttribute> vertexAttributesByLocation;
		signed vertexAttributeStageInBinding;
		bool shouldFlipVertexY;
		bool shouldFlipFragmentY;
		bool isRenderingPoints;

		MetalStageInTranslatorRenderContext() : vertexAttributeStageInBinding(-1), shouldFlipVertexY(true),
												shouldFlipFragmentY(true), isRenderingPoints(false) {}
	};

	/** Converts SPIR-V code to Metal Shading Language Code that uses Stage-In attributes. */
	class MetalStageInTranslator : public MetalTranslator {

	public:
		using MetalTranslator::outputCode;

		/** Outputs code to the specified stream. */
		virtual void outputCode(const Target& target,
								const MetalStageInTranslatorRenderContext& renderContext,
								std::ostream& output,
								std::map<std::string, int>& attributes);

		/** Output the specified instruction.  */
		virtual void outputInstruction(const Target& target,
									   std::map<std::string, int>& attributes,
									   Instruction& inst);

		/** Constructs an instance. Stage is taken from the SPIR-V itself. */
		MetalStageInTranslator(std::vector<uint32_t>& spirv) : MetalTranslator(spirv, EShLangCount) {}

	protected:
		virtual void outputHeader();
		virtual void outputFunctionSignature(bool asDeclaration);
		virtual void outputEntryFunctionSignature(bool asDeclaration);
		virtual void outputLocalFunctionSignature(bool asDeclaration);
		virtual void closeFunctionSignature(bool asDeclaration);
		virtual bool outputFunctionParameters(bool asDeclaration, bool needsComma);
		virtual bool outputLooseUniformStruct();
		virtual void outputUniformBuffers();
		virtual void outputVertexInStructs();
		virtual bool outputStageInStruct();
		virtual bool outputStageOutStruct();
		virtual signed getMetalResourceIndex(Variable& variable, spv::Op rezType);
		EShLanguage stageFromSPIRVExecutionModel(spv::ExecutionModel execModel);
		bool isUniformBufferMember(Variable& var, Type& type);
		bool paramComma(std::ostream* out, bool needsComma);

		MetalStageInTranslatorRenderContext* _pRenderContext;
		std::unordered_map<unsigned, MetalVertexInStruct> _vertexInStructs;
		unsigned _nextMTLBufferIndex;
		unsigned _nextMTLTextureIndex;
		unsigned _nextMTLSamplerIndex;
		bool _hasLooseUniforms;
		bool _hasStageIn;
		bool _hasStageOut;
		bool _isEntryFunction;
	};

	/**
	 * Cleans the specified shader function name so it can be used as as an MSL function name.
	 * The cleansed name is returned. The original name is left unmodified.
	 */
	std::string& cleanMSLFuncName(std::string& funcName);

	typedef std::pair<const unsigned, Variable> KrafixVarPair;

	/**
	 * When sorting a map of variables, returns whether the variable in the first map pair
	 * has a lower location than the second. Built-in variables have no location and will
	 * appear at the end of the list.
	 */
	bool compareByLocation(KrafixVarPair* vp1, KrafixVarPair* vp2);

}
