#include "CStyleTranslator.h"
#include <SPIRV/GLSL450Lib.h>

using namespace krafix;

typedef unsigned id;

CStyleTranslator::CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {

}

std::string CStyleTranslator::indexName(const std::vector<unsigned>& indices) {
	std::stringstream str;
	for (unsigned i = 0; i < indices.size(); ++i) {
		str << "[";
		str << indices[i];
		str << "]";
	}
	return str.str();
}

void CStyleTranslator::indent(std::ostream* out) {
	for (int i = 0; i < indentation; ++i) {
		(*out) << "\t";
	}
}

void CStyleTranslator::output(std::ostream* out) {
	outputting = true;
	indent(out);
}

std::string CStyleTranslator::getReference(id _id) {
	if (references.find(_id) == references.end()) {
		std::stringstream str;
		str << "_" << _id;
		return str.str();
	}
	else {
		return references[_id];
	}
}

void CStyleTranslator::startFunction(std::string name) {
	tempout = out;
	Function* func = new Function;
	func->name = name;
	functions.push_back(func);
	out = &func->text;
}

void CStyleTranslator::endFunction() {
	out = tempout;
}

void CStyleTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

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
	case OpTypeBool: {
		Type t;
		unsigned id = inst.operands[0];
		t.name = "bool";
		types[id] = t;
		break;
	}
	case OpConstant: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		std::string value = "unknown";
		if (strcmp(resultType.name, "float") == 0) {
			float f = *(float*)&inst.operands[2];
			std::stringstream strvalue;
			strvalue << f;
			if (strvalue.str().find('.') == std::string::npos) strvalue << ".0";
			value = strvalue.str();
		}
		if (strcmp(resultType.name, "int") == 0) {
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
	case OpFunctionEnd:
		--indentation;
		output(out);
		(*out) << "}";
		endFunction();
		break;
	case OpCompositeExtract: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id composite = inst.operands[2];
		std::stringstream str;
		std::vector<unsigned> indices;
		for (unsigned i = 3; i < inst.length; ++i) {
			indices.push_back(inst.operands[i]);
		}
		str << getReference(composite) << indexName(indices);
		references[result] = str.str();
		break;
	}
	case OpVectorShuffle: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector1 = inst.operands[2];
		id vector1length = types[vector1].length;
		id vector2 = inst.operands[3];
		id vector2length = types[vector2].length;
		
		std::stringstream str;
		str << resultType.name << "(";
		for (unsigned i = 4; i < inst.length; ++i) {
			id index = inst.operands[i];
			std::vector<unsigned> indices1;
			indices1.push_back(index);
			std::vector<unsigned> indices2;
			indices2.push_back(index - vector1length);
			if (index < vector1length) str << getReference(vector1) << indexName(indices1);
			else str << getReference(vector2) << indexName(indices2);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpFMul: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFAdd: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " + " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpMatrixTimesMatrix: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
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
		types[result] = resultType;
		id vector = inst.operands[2];
		id scalar = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(vector) << " * " << getReference(scalar) << ")";
		references[result] = str.str();
		break;
	}
	case OpFOrdGreaterThan: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " > " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFOrdLessThanEqual: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " <= " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpLogicalAnd: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " && " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFSub: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " - " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpDot: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "dot(" << getReference(op1) << ", " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFDiv: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " / " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpVectorTimesMatrix: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector = inst.operands[2];
		id matrix = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(vector) << " * " << getReference(matrix) << ")";
		references[result] = str.str();
		break;
	}
	case OpSelect: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id condition = inst.operands[2];
		id obj1 = inst.operands[3];
		id obj2 = inst.operands[4];
		std::stringstream str;
		str << "(" << getReference(condition) << " ? " << getReference(obj1) << " : " << getReference(obj2) << ")";
		references[result] = str.str();
		break;
	}
	case OpCompositeInsert: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id object = inst.operands[2];
		id composite = inst.operands[3];
		std::stringstream str;
		str << getReference(object);
		references[result] = str.str();
		std::vector<unsigned> indices;
		for (unsigned i = 4; i < inst.length; ++i) {
			indices.push_back(inst.operands[i]);
		}
		compositeInserts[result] = indices;
		break;
	}
	case OpFunctionCall: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id func = inst.operands[2];
		std::string funcname = names[func].name;
		funcname = funcname.substr(0, funcname.find_first_of('('));
		std::stringstream str;
		str << funcname << "(";
		for (unsigned i = 3; i < inst.length; ++i) {
			str << getReference(inst.operands[i]);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpExtInst: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id set = inst.operands[2];
		{
			using namespace GLSL_STD_450;
			Entrypoints instruction = (Entrypoints)inst.operands[3];
			switch (instruction) {
			case Normalize: {
				std::stringstream str;
				str << "normalize(" << getReference(inst.operands[4]) << ")";
				references[result] = str.str();
				break;
			}
			case Clamp: {
				id x = inst.operands[4];
				id minVal = inst.operands[5];
				id maxVal = inst.operands[6];
				std::stringstream str;
				str << "clamp(" << getReference(x) << ", " << getReference(minVal) << ", " << getReference(maxVal) << ")";
				references[result] = str.str();
				break;
			}
			case Pow: {
				id x = inst.operands[4];
				id y = inst.operands[5];
				std::stringstream str;
				str << "pow(" << getReference(x) << ", " << getReference(y) << ")";
				references[result] = str.str();
				break;
			}
			case InverseSqrt: {
				id x = inst.operands[4];
				std::stringstream str;
				str << "inversesqrt(" << getReference(x) << ")";
				references[result] = str.str();
				break;
			}
			default:
				output(out);
				(*out) << "// Unknown GLSL instruction " << instruction;
				break;
			}
		}
		break;
	}
	case OpFunctionParameter: {
		Parameter param;
		param.type = types[inst.operands[0]];
		param.id = inst.operands[1];
		parameters.push_back(param);
		if (names.find(param.id) != names.end()) {
			references[param.id] = names[param.id].name;
		}
		break;
	}
	case OpLabel: {
		id label = inst.operands[0];
		if (labelStarts.find(inst.operands[0]) != labelStarts.end()) {
			output(out);
			(*out) << labelStarts[inst.operands[0]] << "\n";
			indent(out);
			(*out) << "{ // Label " << label;
			++indentation;
		}
		else {
			output(out);
			(*out) << "// Label " << label;
		}
		break;
	}
	case OpBranch: {
		id branch = inst.operands[0];
		if (merges.find(branch) != merges.end()) {
			--indentation;
			output(out);
			(*out) << "} // Branch to " << branch;
		}
		else {
			output(out);
			(*out) << "// Branch to " << branch;
		}
		break;
	}
	case OpSelectionMerge: {
		output(out);
		id label = inst.operands[0];
		unsigned selection = inst.operands[1];
		(*out) << "// Merge " << label << " " << selection;
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
		if (merges.find(falseLabel) == merges.end()) labelStarts[falseLabel] = "else";
		break;
	}
	case OpReturnValue: {
		output(out);
		(*out) << "return " << getReference(inst.operands[0]) << ";";
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
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " == " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpAccessChain: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id base = inst.operands[2];
		id index = inst.operands[3];
		std::stringstream str;
		str << getReference(base) << "[" << getReference(index) << "]";
		references[result] = str.str();
		break;
	}
	case OpLoad: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = getReference(inst.operands[2]);
		break;
	}
	case OpTypeVoid:
		break;
	case OpEntryPoint:
		entryPoint = inst.operands[1];
		break;
	case OpMemoryModel:
		break;
	case OpExtInstImport:
		break;
	case OpSource:
		break;
	default:
		output(out);
		(*out) << "// Unknown operation " << inst.opcode;
		break;
	}
}
