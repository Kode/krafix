#include "CStyleTranslator.h"
#include <stdio.h>
#include <string.h>

using namespace krafix;

typedef unsigned id;

#ifndef SYS_WINDOWS
void _itoa(int value, char* str, int base) {
	sprintf(str, "%d", value);
}
#endif

namespace {
	int unnamedCount = 0;
}

CStyleTranslator::CStyleTranslator(std::vector<unsigned>& spirv, EShLanguage stage) : Translator(spirv, stage) {}

CStyleTranslator::~CStyleTranslator() {
	// Delete any function objects that were added to the function pointer vector
	for (std::vector<Function*>::iterator iter = functions.begin(), end = functions.end(); iter != end; iter++) {
		delete *iter;
	}
	functions.clear();
}

/** 
 * Associate the specified name with the specified ID, 
 * and ensure the name is unique by appending the ID if needed.
 * This is necessary if the SPIR-V contains duplicate names for intermediate variables.
 */
void CStyleTranslator::addUniqueName(unsigned id, const char* name) {
	std::string uqName = name;
	for (std::map<unsigned, std::string>::iterator iter = uniqueNames.begin(); iter != uniqueNames.end(); iter++) {
		if (iter->second == uqName) {
			if (iter->first != id) {					// If BOTH ID and name are same, leave it
				char idStr[32];
				_itoa(id, idStr, 10);
				uqName = uqName + idStr;				// Otherwise make the name unique...
				uniqueNames[id] = uqName;				// ...and add it
			}
			return;
		}
	}
	uniqueNames[id] = uqName;				// If not found, add name unchanged
}

/**
 * Returns the name associated with the specified ID. If a name does not yet 
 * exist for the ID, a unique name is created from the ID and the prefix string.
 * This is necessary if the SPIR-V does not contain names, or contains duplicates.
 */
std::string& CStyleTranslator::getUniqueName(unsigned id, const char* prefix) {
	std::string& uqName = uniqueNames[id];
	if (uqName == "") {
		char idStr[32];
		_itoa(id, idStr, 10);
		uqName = uqName + idStr;				// Otherwise make the name unique...
		uniqueNames[id] = uqName;
	}
	return uqName;
}

/** Returns the name of the specified variable, creating a unique name if necessary. */
std::string& CStyleTranslator::getVariableName(unsigned id) {
	return getUniqueName(id, "var");
}

std::string& CStyleTranslator::getFunctionName(unsigned id) {
	std::string& funcName =  getUniqueName(id, "func");
	size_t endPos = funcName.find_first_of('(');
	if (endPos != std::string::npos) {
		funcName = funcName.substr(0, endPos);
		uniqueNames[id] = funcName;
	}
	return funcName;
}

std::string CStyleTranslator::getNextTempName() {
	return tempNamePrefix + std::to_string(tempNameIndex++);
}

unsigned CStyleTranslator::getBaseTypeID(unsigned typeID) {
	Type& t = types[typeID];
	return t.ispointer ? t.baseType : typeID;
}

Type& CStyleTranslator::getBaseType(unsigned typeID) {
	Type& t = types[typeID];
	return t.ispointer ? types[t.baseType] : t;
}

/** 
 * Outputs a line containing a unique temp variable assigned from the RHS, 
 * and returns a referenct to the name of the temp variable. 
 */
std::string CStyleTranslator::outputTempVar(std::ostream* out,
											std::string& tmpTypeName,
											const std::string& rhs) {
	std::string tmpName = getNextTempName();
	indent(out);
	(*out) << tmpTypeName << " " << tmpName << " = " << rhs << ";\n";
	return tmpName;
}

std::string CStyleTranslator::indexName(Type& type, const std::vector<unsigned>& indices) {
	std::vector<std::string> stringindices;
	for (unsigned i = 0; i < indices.size(); ++i) {
		char a[32];
		_itoa(indices[i], a, 10);
		stringindices.push_back(a);
	}
	return indexName(type, stringindices);
}

std::string CStyleTranslator::indexName(Type& type, const std::vector<std::string>& indices) {
	std::stringstream str;
	for (unsigned i = 0; i < indices.size(); ++i) {
		int numindex = -1;
		if (indices[i][0] >= '0' && indices[i][0] <= '9') numindex = atoi(indices[i].c_str());
		if (numindex >= 0 && (!type.isarray || i > 0) && type.members.find(numindex) != type.members.end()) {
			if (strncmp(type.name.c_str(), "gl_", 3) != 0 || i > 0) str << ".";
			str << std::get<0>(type.members[numindex]);
		}
		else {
			str << "[";
			str << indices[i];
			str << "]";
		}
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
	// guard against an end func not matched to a previous start func
	if (tempout) {
		out = tempout;
		tempout = NULL;
	}
}

/**
 * Populates the specified array of image operands from the specified instruction,
 * by reading optional instruction operands, starting at the specified operand index.
 */
void CStyleTranslator::extractImageOperands(ImageOperandsArray& imageOperands, Instruction& inst, unsigned opIdxStart) {

	if (inst.length <= opIdxStart) { return; }	// No image operands

	// The first operand contains a bit mask of image operands to follow.
	// For each possible image operand bit position, see if the bit has been set,
	// and add the reference string derived from the cooresponding operand ID.
	// Operand ID's are pulled from the list of instruction operands in the order
	// of the active bit mask bit positions.
	unsigned opIdx = opIdxStart;
	unsigned imgOps = inst.operands[opIdx++];
	unsigned opShCnt = (unsigned)imageOperands.size();
	std::string imgOpArgs;
	for (unsigned opShIdx = 0; opShIdx < opShCnt; opShIdx++) {
		imgOpArgs.clear();
		unsigned imgOpMask = ((unsigned)0x1) << opShIdx;
		if ((imgOps & imgOpMask) == imgOpMask) {
			switch (opShIdx) {		// Pull two image operand values
				case spv::ImageOperandsGradShift:
					imgOpArgs.append(references[inst.operands[opIdx++]]);
					imgOpArgs.append(", ");
					imgOpArgs.append(references[inst.operands[opIdx++]]);
					break;
				default:			// Pull one image operand value
					imgOpArgs.append(references[inst.operands[opIdx++]]);
					break;
			}
		}
		imageOperands[opShIdx] = imgOpArgs;
	}
}

void CStyleTranslator::outputLibraryInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst, GLSLstd450 entrypoint) {
	id result = inst.operands[1];
	switch (entrypoint) {
	case GLSLstd450FAbs: {
		std::stringstream str;
		str << "abs(" << getReference(inst.operands[4]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Normalize: {
		std::stringstream str;
		str << "normalize(" << getReference(inst.operands[4]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FClamp: {
		id x = inst.operands[4];
		id minVal = inst.operands[5];
		id maxVal = inst.operands[6];
		std::stringstream str;
		str << "clamp(" << getReference(x) << ", " << getReference(minVal) << ", " << getReference(maxVal) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Pow: {
		id x = inst.operands[4];
		id y = inst.operands[5];
		std::stringstream str;
		str << "pow(" << getReference(x) << ", " << getReference(y) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450InverseSqrt: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "inversesqrt(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FMin: {
		std::stringstream str;
		str << "min(" << getReference(inst.operands[4]) << ", " << getReference(inst.operands[5]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Cross: {
		id x = inst.operands[4];
		id y = inst.operands[5];
		std::stringstream str;
		str << "cross(" << getReference(x) << ", " << getReference(y) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Sin: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "sin(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Cos: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "cos(" << getReference(x)  << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Tan: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "tan(" << getReference(x)  << ")";
		references[result] = str.str();
		break;
		}
	case GLSLstd450Asin: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "asin(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Sqrt: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "sqrt(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Length: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "length(" << getReference(x) << ")"; //TODO
		references[result] = str.str();
		break;
	}
	case GLSLstd450Exp2: {
		std::stringstream str;
		str << "exp2(" << getReference(inst.operands[4]) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Distance: {
		std::stringstream str;
		id p0 = inst.operands[4];
		id p1 = inst.operands[5];
		str << "distance(" << getReference(p0) << ", " << getReference(p1) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FMix: {
		std::stringstream str;
		id x = inst.operands[4];
		id y = inst.operands[5];
		id a = inst.operands[6];
		str << "mix(" << getReference(x) << ", " << getReference(y) << ", " << getReference(a) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Floor: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "floor(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Exp: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "exp(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Log2: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "log2(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FSign: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "sign(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Ceil: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "ceil(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Fract: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "fract(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450FMax: {
		std::stringstream str;
		id p0 = inst.operands[4];
		id p1 = inst.operands[5];
		str << "max(" << getReference(p0) << ", " << getReference(p1) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Step: {
		std::stringstream str;
		id edge = inst.operands[4];
		id x = inst.operands[5];
		str << "step(" << getReference(edge) << ", " << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450SmoothStep: {
		std::stringstream str;
		id edge0 = inst.operands[4];
		id edge1 = inst.operands[5];
		id x = inst.operands[6];
		str << "smoothstep(" << getReference(edge0) << ", " << getReference(edge1) << ", " << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Reflect: {
		std::stringstream str;
		id I = inst.operands[4];
		id N = inst.operands[5];
		str << "reflect(" << getReference(I) << ", " << getReference(N) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Acos: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "acos(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Atan: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "atan(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Atan2: {
		id y = inst.operands[4];
		id x = inst.operands[5];
		std::stringstream str;
		str << "atan(" << getReference(y) << ", " << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450MatrixInverse: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "inverse(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	case GLSLstd450Determinant: {
		id x = inst.operands[4];
		std::stringstream str;
		str << "determinant(" << getReference(x) << ")";
		references[result] = str.str();
		break;
	}
	default:
		output(out);
		(*out) << "// Unknown GLSL instruction " << entrypoint;
		break;
	}
}

void CStyleTranslator::outputInstruction(const Target& target, std::map<std::string, int>& attributes, Instruction& inst) {
	using namespace spv;

	switch (inst.opcode) {

	case OpName: {
		unsigned id = inst.operands[0];
		const char* name = inst.string;
		
		Name n;
		if (strcmp(inst.string, "") == 0) {
			char unname[101];
			strcpy(unname, "_unnamed");
			_itoa(unnamedCount++, &unname[8], 10);
			n.name = unname;
		}
		else {
			n.name = inst.string;
		}
		names[id] = n;
		addUniqueName(id, inst.string);		// Also add to array of unique names
		break;
	}
	case OpTypePointer: {
		unsigned id = inst.operands[0];
		unsigned reftype = inst.operands[2];
		Type t = types[reftype];	// Pass through referenced type
		t.opcode = inst.opcode;		// ...except OpCode...
		t.baseType = reftype;		// ...and base type
		t.ispointer = true;			// ...and pointer indicator
		types[id] = t;
		names[id] = names[inst.operands[2]];
		break;
	}
	case OpTypeVoid: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "void";
		break;
	}
	case OpTypeFloat: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "float";
		t.byteSize = 4;
		break;
	}
	case OpTypeInt: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "int";
		t.byteSize = 4;
		break;
	}
	case OpTypeBool: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "bool";
		t.byteSize = 1;
		break;
	}
	case OpTypeStruct: {
		unsigned typeId = inst.operands[0];
		Type& t = types[typeId];
		t.opcode = inst.opcode;
		t.name = names[typeId].name;
		unsigned mbrCnt = inst.length - 1;
		t.length = mbrCnt;
		for (unsigned mbrIdx = 0, opIdx = 1; mbrIdx < mbrCnt; mbrIdx++, opIdx++) {
			unsigned mbrId = getMemberId(typeId, mbrIdx);
			unsigned mbrTypeId = inst.operands[opIdx];
			members[mbrId].type = mbrTypeId;
			Type& mbrType = types[mbrTypeId];
			t.byteSize += mbrType.byteSize;
		}
		
		for (unsigned i = 1; i < inst.length; ++i) {
			Type& membertype = types[inst.operands[i]];
			std::get<1>(t.members[i - 1]) = membertype;
		}
		break;
	}
	case OpMemberName: {
		if (strcmp(inst.string, "") != 0) {
			unsigned typeId = inst.operands[0];
			unsigned member = inst.operands[1];
			unsigned mbrId = getMemberId(typeId, member);
			members[mbrId].name = inst.string;
			references[mbrId] = inst.string;

			Type& type = types[inst.operands[0]];
			std::get<0>(type.members[member]) = (char*)&inst.operands[2];
		}
		break;
	}
	case OpMemberDecorate: {
		unsigned typeId = inst.operands[0];
		unsigned member = inst.operands[1];
		unsigned mbrId = getMemberId(typeId, member);
		Decoration decoration = (Decoration)inst.operands[2];
		switch (decoration) {
			case DecorationBuiltIn: {
				Member& mbr = members[mbrId];
				mbr.builtin = true;
				mbr.builtinType = (BuiltIn)inst.operands[3];
				break;
			}
			case spv::DecorationColMajor:
				members[mbrId].isColumnMajor = true;
				break;
			case spv::DecorationRowMajor:
				members[mbrId].isColumnMajor = false;
				break;
			default:
				break;
		}
		break;
	}
	case OpConstant: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		std::string value = "unknown";
		if (resultType.name == "float") {
			float f = *(float*)&inst.operands[2];
			std::stringstream strvalue;
			strvalue << f;
			if (strvalue.str().find_first_of(".e") == std::string::npos) strvalue << ".0";
			value = strvalue.str();
		}
		if (resultType.name == "int") {
			std::stringstream strvalue;
			strvalue << *(int*)&inst.operands[2];
			value = strvalue.str();
		}
		references[result] = value;
		break;
	}
	case OpConstantTrue: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = "true";
		break;
	}
	case OpConstantFalse: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = "false";
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
	case OpExecutionMode: {
		output(out);
		switch (inst.operands[1]) {
		case ExecutionModeInvocations:

			break;
		case ExecutionModeTriangles:
			(*out) << "layout(triangles) in;";
			break;
		case ExecutionModeOutputTriangleStrip:
			if (stage == EShLangGeometry) {
				(*out) << "layout(triangle_strip, max_vertices = 3) out;";
			}
			break;
		case ExecutionModeSpacingEqual:
			if (stage == EShLangTessEvaluation) {
				(*out) << "layout(equal_spacing) in;";
			}
			break;
		case ExecutionModeVertexOrderCw:
			if (stage == EShLangTessEvaluation) {
				(*out) << "layout(cw) in;";
			}
			break;
		case ExecutionModeOutputVertices:
			if (stage == EShLangTessControl) {
				(*out) << "layout(vertices = " << inst.operands[2] << ") out;";
			}
			break;
		default:
			(*out) << "// Unknown execution mode " << inst.operands[1];
		}
		break;
	}
	case OpTypeArray: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "unknownarray";
		t.isarray = true;
		Type& subtype = types[inst.operands[1]];
		t.length = atoi(references[inst.operands[2]].c_str());
		if (subtype.name != "") {
			t.name = subtype.name;
		}
		t.members = subtype.members;
		t.byteSize = subtype.byteSize * t.length;
		break;
	}
	case OpTypeVector: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "vec?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name == "float" && inst.operands[2] == 2) {
			t.name = "vec2";
			t.length = 2;
		}
		else if (subtype.name == "float" && inst.operands[2] == 3) {
			t.name = "vec3";
			t.length = 3;
		}
		else if (subtype.name == "float" && inst.operands[2] == 4) {
			t.name = "vec4";
			t.length = 4;
		}
		t.byteSize = subtype.byteSize * t.length;
		break;
	}
	case OpTypeMatrix: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "mat?";
		Type subtype = types[inst.operands[1]];
		if (subtype.name == "vec2" && inst.operands[2] == 2) {
			t.name = "mat2";
			t.length = 4;
		}
		if (subtype.name == "vec3" && inst.operands[2] == 3) {
			t.name = "mat3";
			t.length = 4;
		}
		else if (subtype.name == "vec4" && inst.operands[2] == 4) {
			t.name = "mat4";
			t.length = 4;
		}
		t.byteSize = subtype.byteSize * t.length;
		break;
	}
	case OpTypeImage: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		bool video = inst.length >= 8 && inst.operands[8] == 1;
		bool depth = inst.length >= 3 && inst.operands[3] == 1;
		if (video && target.system == Android) {
			t.name = "samplerExternalOES";
		}
		else if (depth) {
			t.name = "sampler2DShadow";
		}
		else {
			t.name = "sampler2D";
		}
		break;
	}
	case OpTypeSampler: {
		break;
	}
	case OpTypeSampledImage: {
		unsigned id = inst.operands[0];
		unsigned image = inst.operands[1];
		Type t = types[image];		// Pass through image type...
		t.opcode = inst.opcode;		// ...except OpCode...
		t.baseType = image;			// ...and base type
		types[id] = t;
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
		v.declared = true; //v.storage == StorageClassInput || v.storage == StorageClassOutput || v.storage == StorageClassUniformConstant;
		if (names.find(result) != names.end()) {
			if (target.version >= 300 && v.storage == StorageClassOutput && stage == EShLangFragment) {
				names[result].name = "krafix_FragColor";
			}
			std::stringstream name;
			name << names[result].name;
			if (v.storage == StorageClassFunction && getReference(result) != "param") name << "_" << result;
			references[result] = name.str();
		}
		if (v.storage == StorageClassFunction && getReference(result) != "param") {
			output(out);
			Type t = types[v.type];
			if (t.isarray) {
				(*out) << t.name << " " << getReference(result) << "[" << t.length << "];";
			}
			else {
				(*out) << t.name << " " << getReference(result) << ";";
			}
		}
		break;
	}
	case OpPhi: {
		Type resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		output(out);
		(*out) << resultType.name << " " << getReference(result) << ";\n";

		bool first = true;
		
		for (unsigned i = 2; i < inst.length; i += 2) {
			id variable = inst.operands[i];
			id parent = inst.operands[i + 1];
		
			if (labelStarts.find(parent) != labelStarts.end()) {
				indent(out);
				if (!first) (*out) << "else ";
				(*out) << labelStarts[parent] << "\n";
				++indentation;
				indent(out);
				(*out) << getReference(result) << " = " << getReference(variable) << ";\n";
				--indentation;

				first = false;
			}
		}

		for (unsigned i = 2; i < inst.length; i += 2) {
			id variable = inst.operands[i];
			id parent = inst.operands[i + 1];
			
			if (labelStarts.find(parent) == labelStarts.end()) {
				indent(out);
				(*out) << "else\n";
				++indentation;
				indent(out);
				(*out) << getReference(result) << " = " << getReference(variable) << ";\n";
				--indentation;
			}
		}

		references[result] = getReference(result);
		break;
	}
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
		str << getReference(composite) << indexName(types[composite], indices);
		references[result] = str.str();
		break;
	}
	case OpVectorShuffle: {
		Type& resultType = types[inst.operands[0]];
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
			if (index < vector1length) str << getReference(vector1) << indexName(types[vector1], indices1);
			else str << getReference(vector2) << indexName(types[vector2], indices2);
			if (i < inst.length - 1) str << ", ";
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpFMul: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpIMul: {
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id operand1 = inst.operands[2];
		id operand2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(operand1) << " * " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpMatrixTimesScalar: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id matrix = inst.operands[2];
		id scalar = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(matrix) << " * " << getReference(scalar) << ")";
		references[result] = str.str();
		break;
	}
	case OpVectorTimesScalar: {
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " <= " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpFOrdNotEqual: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id op1 = inst.operands[2];
		id op2 = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(op1) << " != " << getReference(op2) << ")";
		references[result] = str.str();
		break;
	}
	case OpLogicalAnd: {
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id vector = inst.operands[2];
		id matrix = inst.operands[3];
		std::stringstream str;
		str << "(" << getReference(vector) << " * " << getReference(matrix) << ")";
		references[result] = str.str();
		break;
	}
	case OpConvertSToF: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		id value = inst.operands[2];
		std::stringstream str;
		str << "float(" << getReference(value) << ")";
		references[result] = str.str();
		break;
	}
	case OpTranspose: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id matrix = inst.operands[2];
		std::stringstream str;
		str << "transpose(" << getReference(matrix) << ")";
		references[result] = str.str();
		break;
	}
	case OpSelect: {
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
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
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id set = inst.operands[2];
		{
			GLSLstd450 instruction = (GLSLstd450)inst.operands[3];
			outputLibraryInstruction(target, attributes, inst, instruction);
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
			if (labelStarts[inst.operands[0]].at(0) == '}') {
				--indentation;
			}
			output(out);
			(*out) << labelStarts[inst.operands[0]] << "\n";
			indent(out);
			(*out) << "{ // Label " << label;
			++indentation;
		}
		else if (merges.find(label) != merges.end()) {
			--indentation;
			output(out);
			(*out) << "} // Label " << label;
		}
		else {
			output(out);
			(*out) << "// Label " << label;
		}
		break;
	}
	case OpBranch: {
		id branch = inst.operands[0];
		output(out);
		if (merges.find(branch) != merges.end() && merges.find(branch)->second.loop) (*out) << "break; // Branch to " << branch;
		else (*out) << "// Branch to " << branch;
		break;
	}
	case OpSelectionMerge: {
		output(out);
		id label = inst.operands[0];
		unsigned selection = inst.operands[1];
		(*out) << "// Merge " << label << " " << selection;
		Merge merge;
		merge.loop = false;
		merges[label] = merge;
		break;
	}
	case OpLoopMerge: {
		output(out);
		id label = inst.operands[0];
		unsigned selection = inst.operands[1];
		(*out) << "// Merge " << label << " " << selection;
		Merge merge;
		merge.loop = true;
		merges[label] = merge;
		break;
	}
	case OpBranchConditional: {
		id condition = inst.operands[0];
		id trueLabel = inst.operands[1];
		id falseLabel = inst.operands[2];

		bool foundMerge = false;
		bool loop = false;
		if (merges.find(falseLabel) != merges.end()) {
			foundMerge = true;
			loop = merges[falseLabel].loop;
		}

		std::stringstream _true;
		if (loop) _true << "while (" << getReference(condition) << ") // true: " << trueLabel << " false: " << falseLabel;
		else _true << "if (" << getReference(condition) << ") // true: " << trueLabel << " false: " << falseLabel;
		labelStarts[trueLabel] = _true.str();
		if (!foundMerge) {
			std::stringstream str;
			str << "}\n";
			for (int i = 0; i < indentation; ++i) str << "\t";
			str << "else";
			labelStarts[falseLabel] = str.str();
		}
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
		switch (decoration) {
			case DecorationBuiltIn: {
				Variable& var = variables[target];
				var.builtin = true;
				var.builtinType = (BuiltIn)inst.operands[2];
				if (var.builtinType == BuiltInVertexId) { vtxIdVarId = target; }
				if (var.builtinType == BuiltInInstanceId) { instIdVarId = target; }
				break;
			}
			case DecorationLocation:
				variables[target].location = inst.operands[2];
				break;
			case DecorationDescriptorSet:
				variables[target].descriptorSet = inst.operands[2];
				break;
			case DecorationBinding:
				variables[target].binding = inst.operands[2];
				break;
			default:
				break;
		}
		break;
	}
	case OpTypeFunction: {
		unsigned id = inst.operands[0];
		Type& t = types[id];
		t.opcode = inst.opcode;
		t.name = "function";
		break;
	}
	case OpIEqual: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " == " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpIAdd: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " + " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFOrdLessThan: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " < " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpSLessThan: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " < " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpSLessThanEqual: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " <= " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFNegate: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand = inst.operands[2];
		std::stringstream str;
		str << "-" << getReference(operand);
		references[result] = str.str();
		break;
	}
	case OpAccessChain: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id base = inst.operands[2];
		std::stringstream str;
		if (strncmp(types[base].name.c_str(), "gl_", 3) != 0 || types[base].isarray) str << getReference(base);
		std::vector<std::string> indices;
		for (unsigned i = 3; i < inst.length; ++i) {
			/*std::string reference = getReference(inst.operands[i]);
			if (reference[0] >= '0' && reference[0] <= 9) indices.push_back(atoi(reference.c_str()));
			else*/ indices.push_back(getReference(inst.operands[i]));
		}
		str << indexName(types[base], indices);
		references[result] = str.str();
		break;
	}
	case OpLoad: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		references[result] = getReference(inst.operands[2]);
		break;
	}
	case OpFOrdEqual: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " == " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFOrdGreaterThanEqual: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " >= " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpFMod: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << "mod(" << getReference(operand1) << ", " << getReference(operand2) << ")";
		references[result] = str.str();
		break;
	}
	case OpISub: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " - " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpLogicalOr: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned operand1 = inst.operands[2];
		unsigned operand2 = inst.operands[3];
		std::stringstream str;
		str << getReference(operand1) << " || " << getReference(operand2);
		references[result] = str.str();
		break;
	}
	case OpConvertFToS: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned value = inst.operands[2];
		std::stringstream str;
		str << "int(" << getReference(value) << ")";
		references[result] = str.str();
		break;
	}
	case OpLogicalNot: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		unsigned value = inst.operands[2];
		std::stringstream str;
		str << "!" << getReference(value);
		references[result] = str.str();
		break;
		break;
	}
	case OpEmitVertex:
		output(out);
		(*out) << "EmitVertex();";
		break;
	case OpEndPrimitive:
		output(out);
		(*out) << "EndPrimitive();";
		break;
	case OpFunction: {
		firstLabel = true;
		parameters.clear();
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		if (result == entryPoint) {
			references[result] = "main";
			funcName = "main";
			funcType = "void";
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
		str << "(" << getReference(matrix) << " * " << getReference(vector) << ")";
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
		if (target.version < 300) str << "texture2D";
		else str << "texture";
		str << "(" << getReference(sampler) << ", " << getReference(coordinate);
		if (inst.length > 5) {
			id bias = inst.operands[5];
			str << ", " << getReference(bias);
		}
		str << ")";
		references[result] = str.str();
		break;
	}
	case OpImageSampleExplicitLod: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id sampler = inst.operands[2];
		id coordinate = inst.operands[3];
		id lod = inst.operands[5];
		std::stringstream str;
		if (target.system == HTML5) str << "texture2DLodEXT";
		else if (target.version < 300) str << "texture2DLod";
		else str << "textureLod";
		str << "(" << getReference(sampler) << ", " << getReference(coordinate) << ", " << getReference(lod) << ")";
		references[result] = str.str();
		break;
	}
	case OpImageSampleDrefImplicitLod: {
        Type& resultType = types[inst.operands[0]];
        id result = inst.operands[1];
        types[result] = resultType;
        id sampler = inst.operands[2];
        id coordinate = inst.operands[3];
        std::stringstream str;
        if (target.version < 300) str << "shadow2D";
        else str << "texture";
        str << "(" << getReference(sampler) << ", " << getReference(coordinate);
        if (inst.length > 5) {
            id bias = inst.operands[5];
            str << ", " << getReference(bias);
        }
        str << ")";
        references[result] = str.str();
        break;
    }
	case OpDPdx: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id p = inst.operands[2];
		std::stringstream str;
		str << "dFdx(" << getReference(p) << ")";
		references[result] = str.str();
		break;
	}
	case OpDPdy: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		id p = inst.operands[2];
		std::stringstream str;
		str << "dFdy(" << getReference(p) << ")";
		references[result] = str.str();
		break;
	}
	case OpUndef: {
		Type& resultType = types[inst.operands[0]];
		id result = inst.operands[1];
		types[result] = resultType;
		if (resultType.name == "bool") {
			references[result] = "false";
		}
		else {
			references[result] = "0";
		}
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
				Type type;
				(*out) << "gl_FragColor" << indexName(type, compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
			}
			else {
				(*out) << "gl_FragColor" << " = " << getReference(inst.operands[1]) << ";";
			}
		}
		else {
			output(out);
			if (compositeInserts.find(inst.operands[1]) != compositeInserts.end()) {
				(*out) << getReference(inst.operands[0]) << indexName(types[inst.operands[0]], compositeInserts[inst.operands[1]]) << " = " << getReference(inst.operands[1]) << ";";
			}
			else {
				(*out) << getReference(inst.operands[0]) << " = " << getReference(inst.operands[1]) << ";";
			}
		}
		break;
	}
	case OpEntryPoint:
		entryPoint = inst.operands[1];
		break;
	case OpMemoryModel:
		break;
	case OpExtInstImport:
		break;
	case OpSource:
		break;
	case OpCapability:
		break;
	case OpString:
		break;
	case OpSourceExtension:
		break;
	default:
		output(out);
		(*out) << "// Unknown operation " << inst.opcode;
		break;
	}
}
