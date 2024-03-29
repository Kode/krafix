//
// Copyright (C) 2002-2005  3Dlabs Inc. Ltd.
// Copyright (C) 2013-2016 LunarG, Inc.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

// this only applies to the standalone wrapper, not the front end in general
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "./../glslang/StandAlone/ResourceLimits.h"
#include "./../glslang/StandAlone/Worklist.h"
#include "./../glslang/Include/ShHandle.h"
#include "./../glslang/Include/revision.h"
#include "./../glslang/Public/ShaderLang.h"
#include "../SPIRV/GlslangToSpv.h"
#include "../SPIRV/GLSL.std.450.h"
#include "../SPIRV/doc.h"
#include "../SPIRV/disassemble.h"
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <array>
#include <sstream>

#include "../glslang/OSDependent/osinclude.h"

#include "SpirVTranslator.h"
#include "GlslTranslator.h"
#include "GlslTranslator2.h"
#include "HlslTranslator.h"
#include "HlslTranslator2.h"
#include "AgalTranslator.h"
#include "MetalTranslator.h"
#include "MetalTranslator2.h"
#include "VarListTranslator.h"
#include "JavaScriptTranslator.h"
#include "JavaScriptTranslator2.h"

#include "../SPIRV-Cross/spirv_common.hpp"

extern "C" {
	SH_IMPORT_EXPORT void ShOutputHtml();
}

// Command-line options
enum TOptions {
	EOptionNone = 0,
	EOptionIntermediate = (1 << 0),
	EOptionSuppressInfolog = (1 << 1),
	EOptionMemoryLeakMode = (1 << 2),
	EOptionRelaxedErrors = (1 << 3),
	EOptionGiveWarnings = (1 << 4),
	EOptionLinkProgram = (1 << 5),
	EOptionMultiThreaded = (1 << 6),
	EOptionDumpConfig = (1 << 7),
	EOptionDumpReflection = (1 << 8),
	EOptionSuppressWarnings = (1 << 9),
	EOptionDumpVersions = (1 << 10),
	EOptionSpv = (1 << 11),
	EOptionHumanReadableSpv = (1 << 12),
	EOptionVulkanRules = (1 << 13),
	EOptionDefaultDesktop = (1 << 14),
	EOptionOutputPreprocessed = (1 << 15),
	EOptionOutputHexadecimal = (1 << 16),
	EOptionReadHlsl = (1 << 17),
	EOptionCascadingErrors = (1 << 18),
	EOptionAutoMapBindings = (1 << 19),
	EOptionFlattenUniformArrays = (1 << 20),
	EOptionNoStorageFormat = (1 << 21),
	EOptionKeepUncalled = (1 << 22),
};

//
// Return codes from main/exit().
//
enum TFailCode {
	ESuccess = 0,
	EFailUsage,
	EFailCompile,
	EFailLink,
	EFailCompilerCreate,
	EFailThreadCreate,
	EFailLinkerCreate
};

//
// Forward declarations.
//
EShLanguage FindLanguage(const std::string& name, bool parseSuffix = true);
void CompileFile(const char* fileName, ShHandle);
void usage();
void FreeFileData(char** data);
char** ReadFileData(const char* fileName);
void InfoLogMsg(const char* msg, const char* name, const int num);

// Globally track if any compile or link failure.
bool CompileFailed = false;
bool LinkFailed = false;

static bool quiet = false;
static bool debugMode = false;
static bool outputSpirv = false;

// Use to test breaking up a single shader file into multiple strings.
// Set in ReadFileData().
int NumShaderStrings;

TBuiltInResource Resources;
std::string ConfigFile;

//
// Parse either a .conf file provided by the user or the default from glslang::DefaultTBuiltInResource
//
void ProcessConfigFile()
{
	char** configStrings = 0;
	char* config = 0;
	if (ConfigFile.size() > 0) {
		configStrings = ReadFileData(ConfigFile.c_str());
		if (configStrings)
			config = *configStrings;
		else {
			printf("Error opening configuration file; will instead use the default configuration\n");
			usage();
		}
	}

	if (config == 0) {
		Resources = glslang::DefaultTBuiltInResource;
		return;
	}

	glslang::DecodeResourceLimits(&Resources, config);

	if (configStrings)
		FreeFileData(configStrings);
	else
		delete[] config;
}

// thread-safe list of shaders to asynchronously grab and compile
glslang::TWorklist Worklist;

// array of unique places to leave the shader names and infologs for the asynchronous compiles
glslang::TWorkItem** Work = 0;
int NumWorkItems = 0;

int Options = 0;
const char* ExecutableName = nullptr;
const char* binaryFileName = nullptr;
const char* entryPointName = nullptr;
const char* sourceEntryPointName = nullptr;
const char* shaderStageName = nullptr;
const char* variableName = nullptr;

std::array<unsigned int, EShLangCount> baseSamplerBinding;
std::array<unsigned int, EShLangCount> baseTextureBinding;
std::array<unsigned int, EShLangCount> baseImageBinding;
std::array<unsigned int, EShLangCount> baseUboBinding;
std::array<unsigned int, EShLangCount> baseSsboBinding;

//
// Create the default name for saving a binary if -o is not provided.
//
const char* GetBinaryName(EShLanguage stage)
{
	const char* name;
	if (binaryFileName == nullptr) {
		switch (stage) {
		case EShLangVertex:          name = "vert.spv";    break;
		case EShLangTessControl:     name = "tesc.spv";    break;
		case EShLangTessEvaluation:  name = "tese.spv";    break;
		case EShLangGeometry:        name = "geom.spv";    break;
		case EShLangFragment:        name = "frag.spv";    break;
		case EShLangCompute:         name = "comp.spv";    break;
		default:                     name = "unknown";     break;
		}
	}
	else
		name = binaryFileName;

	return name;
}

//
// *.conf => this is a config file that can set limits/resources
//
bool SetConfigFile(const std::string& name)
{
	if (name.size() < 5)
		return false;

	if (name.compare(name.size() - 5, 5, ".conf") == 0) {
		ConfigFile = name;
		return true;
	}

	return false;
}

//
// Give error and exit with failure code.
//
void Error(const char* message)
{
	printf("%s: Error %s (use -h for usage)\n", ExecutableName, message);
	exit(EFailUsage);
}

//
// Process an optional binding base of the form:
//   --argname [stage] base
// Where stage is one of the forms accepted by FindLanguage, and base is an integer
//
void ProcessBindingBase(int& argc, char**& argv, std::array<unsigned int, EShLangCount>& base)
{
	if (argc < 2)
		usage();

	if (!isdigit(argv[1][0])) {
		if (argc < 3) // this form needs one more argument
			usage();

		// Parse form: --argname stage base
		const EShLanguage lang = FindLanguage(argv[1], false);
		base[lang] = atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}
	else {
		// Parse form: --argname base
		for (int lang = 0; lang < EShLangCount; ++lang)
			base[lang] = atoi(argv[1]);

		argc--;
		argv++;
	}
}

//
// Do all command-line argument parsing.  This includes building up the work-items
// to be processed later, and saving all the command-line options.
//
// Does not return (it exits) if command-line is fatally flawed.
//
void ProcessArguments(int argc, char* argv[])
{
	baseSamplerBinding.fill(0);
	baseTextureBinding.fill(0);
	baseImageBinding.fill(0);
	baseUboBinding.fill(0);
	baseSsboBinding.fill(0);

	ExecutableName = argv[0];
	NumWorkItems = argc;  // will include some empties where the '-' options were, but it doesn't matter, they'll be 0
	Work = new glslang::TWorkItem * [NumWorkItems];
	for (int w = 0; w < NumWorkItems; ++w)
		Work[w] = 0;

	argc--;
	argv++;
	for (; argc >= 1; argc--, argv++) {
		if (argv[0][0] == '-') {
			switch (argv[0][1]) {
			case '-':
			{
				std::string lowerword(argv[0] + 2);
				std::transform(lowerword.begin(), lowerword.end(), lowerword.begin(), ::tolower);

				// handle --word style options
				if (lowerword == "shift-sampler-bindings" || // synonyms
					lowerword == "shift-sampler-binding" ||
					lowerword == "ssb") {
					ProcessBindingBase(argc, argv, baseSamplerBinding);
				}
				else if (lowerword == "shift-texture-bindings" ||  // synonyms
					lowerword == "shift-texture-binding" ||
					lowerword == "stb") {
					ProcessBindingBase(argc, argv, baseTextureBinding);
				}
				else if (lowerword == "shift-image-bindings" ||  // synonyms
					lowerword == "shift-image-binding" ||
					lowerword == "sib") {
					ProcessBindingBase(argc, argv, baseImageBinding);
				}
				else if (lowerword == "shift-ubo-bindings" ||  // synonyms
					lowerword == "shift-ubo-binding" ||
					lowerword == "sub") {
					ProcessBindingBase(argc, argv, baseUboBinding);
				}
				else if (lowerword == "shift-ssbo-bindings" ||  // synonyms
					lowerword == "shift-ssbo-binding" ||
					lowerword == "sbb") {
					ProcessBindingBase(argc, argv, baseSsboBinding);
				}
				else if (lowerword == "auto-map-bindings" ||  // synonyms
					lowerword == "auto-map-binding" ||
					lowerword == "amb") {
					Options |= EOptionAutoMapBindings;
				}
				else if (lowerword == "flatten-uniform-arrays" || // synonyms
					lowerword == "flatten-uniform-array" ||
					lowerword == "fua") {
					Options |= EOptionFlattenUniformArrays;
				}
				else if (lowerword == "no-storage-format" || // synonyms
					lowerword == "nsf") {
					Options |= EOptionNoStorageFormat;
				}
				else if (lowerword == "variable-name" || // synonyms
					lowerword == "vn") {
					Options |= EOptionOutputHexadecimal;
					variableName = argv[1];
					if (argc > 0) {
						argc--;
						argv++;
					}
					else
						Error("no <C-variable-name> provided for --variable-name");
					break;
				}
				else if (lowerword == "source-entrypoint" || // synonyms
					lowerword == "sep") {
					sourceEntryPointName = argv[1];
					if (argc > 0) {
						argc--;
						argv++;
					}
					else
						Error("no <entry-point> provided for --source-entrypoint");
					break;
				}
				else if (lowerword == "keep-uncalled" || // synonyms
					lowerword == "ku") {
					Options |= EOptionKeepUncalled;
				}
				else {
					usage();
				}
			}
			break;
			case 'H':
				Options |= EOptionHumanReadableSpv;
				if ((Options & EOptionSpv) == 0) {
					// default to Vulkan
					Options |= EOptionSpv;
					Options |= EOptionVulkanRules;
					Options |= EOptionLinkProgram;
				}
				break;
			case 'V':
				Options |= EOptionSpv;
				Options |= EOptionVulkanRules;
				Options |= EOptionLinkProgram;
				break;
			case 'S':
				shaderStageName = argv[1];
				if (argc > 0) {
					argc--;
					argv++;
				}
				else
					Error("no <stage> specified for -S");
				break;
			case 'G':
				Options |= EOptionSpv;
				Options |= EOptionLinkProgram;
				// undo a -H default to Vulkan
				Options &= ~EOptionVulkanRules;
				break;
			case 'E':
				Options |= EOptionOutputPreprocessed;
				break;
			case 'c':
				Options |= EOptionDumpConfig;
				break;
			case 'C':
				Options |= EOptionCascadingErrors;
				break;
			case 'd':
				Options |= EOptionDefaultDesktop;
				break;
			case 'D':
				Options |= EOptionReadHlsl;
				break;
			case 'e':
				// HLSL todo: entry point handle needs much more sophistication.
				// This is okay for one compilation unit with one entry point.
				entryPointName = argv[1];
				if (argc > 0) {
					argc--;
					argv++;
				}
				else
					Error("no <entry-point> provided for -e");
				break;
			case 'h':
				usage();
				break;
			case 'i':
				Options |= EOptionIntermediate;
				break;
			case 'l':
				Options |= EOptionLinkProgram;
				break;
			case 'm':
				Options |= EOptionMemoryLeakMode;
				break;
			case 'o':
				binaryFileName = argv[1];
				if (argc > 0) {
					argc--;
					argv++;
				}
				else
					Error("no <file> provided for -o");
				break;
			case 'q':
				Options |= EOptionDumpReflection;
				break;
			case 'r':
				Options |= EOptionRelaxedErrors;
				break;
			case 's':
				Options |= EOptionSuppressInfolog;
				break;
			case 't':
#ifdef _WIN32
				Options |= EOptionMultiThreaded;
#endif
				break;
			case 'v':
				Options |= EOptionDumpVersions;
				break;
			case 'w':
				Options |= EOptionSuppressWarnings;
				break;
			case 'x':
				Options |= EOptionOutputHexadecimal;
				break;
			default:
				usage();
				break;
			}
		}
		else {
			std::string name(argv[0]);
			if (!SetConfigFile(name)) {
				Work[argc] = new glslang::TWorkItem(name);
				Worklist.add(Work[argc]);
			}
		}
	}

	// Make sure that -E is not specified alongside linking (which includes SPV generation)
	if ((Options & EOptionOutputPreprocessed) && (Options & EOptionLinkProgram))
		Error("can't use -E when linking is selected");

	// -o or -x makes no sense if there is no target binary
	if (binaryFileName && (Options & EOptionSpv) == 0)
		Error("no binary generation requested (e.g., -V)");

	if ((Options & EOptionFlattenUniformArrays) != 0 &&
		(Options & EOptionReadHlsl) == 0)
		Error("uniform array flattening only valid when compiling HLSL source.");
}

//
// Translate the meaningful subset of command-line options to parser-behavior options.
//
void SetMessageOptions(EShMessages& messages)
{
	if (Options & EOptionRelaxedErrors)
		messages = (EShMessages)(messages | EShMsgRelaxedErrors);
	if (Options & EOptionIntermediate)
		messages = (EShMessages)(messages | EShMsgAST);
	if (Options & EOptionSuppressWarnings)
		messages = (EShMessages)(messages | EShMsgSuppressWarnings);
	if (Options & EOptionSpv)
		messages = (EShMessages)(messages | EShMsgSpvRules);
	if (Options & EOptionVulkanRules)
		messages = (EShMessages)(messages | EShMsgVulkanRules);
	if (Options & EOptionOutputPreprocessed)
		messages = (EShMessages)(messages | EShMsgOnlyPreprocessor);
	if (Options & EOptionReadHlsl)
		messages = (EShMessages)(messages | EShMsgReadHlsl);
	if (Options & EOptionCascadingErrors)
		messages = (EShMessages)(messages | EShMsgCascadingErrors);
	if (Options & EOptionKeepUncalled)
		messages = (EShMessages)(messages | EShMsgKeepUncalled);
}

//
// Thread entry point, for non-linking asynchronous mode.
//
// Return 0 for failure, 1 for success.
//
unsigned int CompileShaders(void*)
{
	glslang::TWorkItem* workItem;
	while (Worklist.remove(workItem)) {
		ShHandle compiler = ShConstructCompiler(FindLanguage(workItem->name), Options);
		if (compiler == 0)
			return 0;

		CompileFile(workItem->name.c_str(), compiler);

		if (!(Options & EOptionSuppressInfolog))
			workItem->results = ShGetInfoLog(compiler);

		ShDestruct(compiler);
	}

	return 0;
}

// Outputs the given string, but only if it is non-null and non-empty.
// This prevents erroneous newlines from appearing.
void PutsIfNonEmpty(const char* str)
{
	if (str && str[0]) {
		puts(str);
	}
}

// Outputs the given string to stderr, but only if it is non-null and non-empty.
// This prevents erroneous newlines from appearing.
void StderrIfNonEmpty(const char* str)
{
	if (str && str[0]) {
		fprintf(stderr, "%s\n", str);
	}
}

// Simple bundling of what makes a compilation unit for ease in passing around,
// and separation of handling file IO versus API (programmatic) compilation.
struct ShaderCompUnit {
	EShLanguage stage;
	std::string fileName;
	char** text;             // memory owned/managed externally
	const char* fileNameList[1];

	// Need to have a special constructors to adjust the fileNameList, since back end needs a list of ptrs
	ShaderCompUnit(EShLanguage istage, std::string& ifileName, char** itext)
	{
		stage = istage;
		fileName = ifileName;
		text = itext;
		fileNameList[0] = fileName.c_str();
	}

	ShaderCompUnit(const ShaderCompUnit& rhs)
	{
		stage = rhs.stage;
		fileName = rhs.fileName;
		text = rhs.text;
		fileNameList[0] = fileName.c_str();
	}

};

void executeSync(const char* command);
int compileHLSLToD3D9(const char* from, const char* to, const char* source, char* output, int* length, const std::map<std::string, int>& attributes, EShLanguage stage);
int compileHLSLToD3D11(const char* from, const char* to, const char* source, char* output, int* length, const std::map<std::string, int>& attributes, EShLanguage stage, bool debug);

std::string extractFilename(std::string path) {
	int i = (int)path.size() - 1;
	for (; i > 0; --i) {
		if (path[i] == '/' || path[i] == '\\') {
			++i;
			break;
		}
	}
	return path.substr(i, std::string::npos);
}

std::string removeExtension(std::string filename) {
	int i = (int)filename.size() - 1;
	for (; i > 0; --i) {
		if (filename[i] == '.') {
			break;
		}
	}
	if (i == 0) return filename;
	else return filename.substr(0, i);
}

static bool deps = false;
static std::vector<std::string> dependencies;

class KrafixIncluder : public glslang::TShader::Includer {
public:
	KrafixIncluder(std::string from) {
		dir = from;
		for (int i = (int)from.size() - 1; i >= 0; --i) {
			if (dir[i] == '/' || dir[i] == '\\') {
				dir = dir.substr(0, i + 1);
				break;
			}
		}
	}

	IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override {
		return includeLocal(headerName, includerName, inclusionDepth);
	}

	IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override {
		std::string realfilename = dir + headerName;
		if (deps) {
			dependencies.push_back(realfilename);
		}

		std::stringstream content;
		std::string line;
		std::ifstream file(realfilename);
		if (file.is_open()) {
			while (getline(file, line)) {
				content << line << '\n';
			}
			file.close();
		}
		std::string filecontent = content.str();
		char* heapcontent = new char[filecontent.size() + 1];
		strcpy(heapcontent, filecontent.c_str());
		return new IncludeResult(realfilename, heapcontent, content.str().size(), heapcontent);
	}

	void releaseInclude(IncludeResult* result) override {
		delete (char*)result->userData;
		delete result;
	}
private:
	std::string dir;
};

class NullIncluder : public glslang::TShader::Includer {
public:
	NullIncluder() {

	}

	IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override {
		return includeLocal(headerName, includerName, inclusionDepth);
	}

	IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override {
		return nullptr;
	}

	void releaseInclude(IncludeResult* result) override {

	}
};

krafix::ShaderStage shLanguageToShaderStage(EShLanguage lang) {
	switch (lang) {
	case EShLangVertex: return krafix::StageVertex;
	case EShLangTessControl: return krafix::StageTessControl;
	case EShLangTessEvaluation: return krafix::StageTessEvaluation;
	case EShLangGeometry: return krafix::StageGeometry;
	case EShLangFragment: return krafix::StageFragment;
	case EShLangCompute: return krafix::StageCompute;
	case EShLangCount:
	default:
		return krafix::StageCompute;
	}
}

static void writeSpirv(const char* filename, std::vector<unsigned int>& words) {
	std::ofstream out;
	out.open(filename, std::ios::binary | std::ios::out);

	for (unsigned i = 0; i < words.size(); ++i) {
		out.put(words[i] & 0xff);
		out.put((words[i] >> 8) & 0xff);
		out.put((words[i] >> 16) & 0xff);
		out.put((words[i] >> 24) & 0xff);
	}

	out.close();
}

static void preprocessSpirv(std::vector<unsigned int>& spirv) {
	const unsigned OpTypeImage = 25;
	const unsigned OpTypeSampledImage = 27;
	const unsigned OpTypeArray = 28;
	const unsigned OpTypePointer = 32;
	const unsigned OpConstant = 43;
	const unsigned OpVariable = 59;
	const unsigned OpDecorate = 71;

	const unsigned DecorationBinding = 33;

	std::set<unsigned> imageTypes;
	std::map<unsigned, unsigned> constants;
	std::map<unsigned, unsigned> arrayLengths;

	unsigned wordCount = 1;

	for (unsigned index = 5; index < spirv.size(); index += wordCount) {
		wordCount = spirv[index] >> 16;
		int opcode = spirv[index] & 0xffff;

		unsigned* operands = wordCount > 1 ? &spirv[index + 1] : NULL;
		int length = wordCount - 1;

		if (opcode == OpTypeImage) {
			imageTypes.insert(operands[0]);
		}

		if (opcode == OpTypeSampledImage) {
			imageTypes.insert(operands[0]);
		}

		if (opcode == OpConstant) {
			constants[operands[1]] = operands[2];
		}

		if (opcode == OpTypeArray) {
			arrayLengths[operands[0]] = constants[operands[2]];
		}

		if (opcode == OpTypePointer) {
			if (arrayLengths.find(operands[2]) != arrayLengths.end()) {
				arrayLengths[operands[0]] = arrayLengths[operands[2]];
			}
		}

		if (opcode == OpVariable) {
			if (arrayLengths.find(operands[0]) != arrayLengths.end()) {
				arrayLengths[operands[1]] = arrayLengths[operands[0]];
			}
		}
	}

	unsigned binding = 0;

	for (unsigned index = 5; index < spirv.size(); index += wordCount) {
		wordCount = spirv[index] >> 16;
		int opcode = spirv[index] & 0xffff;

		unsigned* operands = wordCount > 1 ? &spirv[index + 1] : NULL;
		int length = wordCount - 1;

		if (opcode == OpDecorate && length >= 2) {
			if (operands[1] == DecorationBinding) {
				operands[2] = binding;
				if (arrayLengths.find(operands[0]) != arrayLengths.end()) {
					binding += arrayLengths[operands[0]];
				}
				else {
					binding += 1;
				}
			}
		}
	}
}

//
// For linking mode: Will independently parse each compilation unit, but then put them
// in the same program and link them together, making at most one linked module per
// pipeline stage.
//
// Uses the new C++ interface instead of the old handle-based interface.
//

void CompileAndLinkShaderUnits(std::vector<ShaderCompUnit> compUnits, krafix::Target target, const char* sourcefilename, const char* filename, const char* tempdir, char* output, int* length,
	glslang::TShader::Includer& includer, const char* defines, bool relax)
{
	// keep track of what to free
	std::list<glslang::TShader*> shaders;

	EShMessages messages = EShMsgDefault;
	SetMessageOptions(messages);

	//
	// Per-shader processing...
	//

	glslang::TProgram& program = *new glslang::TProgram;
	for (auto it = compUnits.cbegin(); it != compUnits.cend(); ++it) {
		const auto& compUnit = *it;
		glslang::TShader* shader = new glslang::TShader(compUnit.stage);
		shader->setStringsWithLengthsAndNames(compUnit.text, NULL, compUnit.fileNameList, 1);
		if (entryPointName) // HLSL todo: this needs to be tracked per compUnits
			shader->setEntryPoint(entryPointName);
		if (sourceEntryPointName)
			shader->setSourceEntryPoint(sourceEntryPointName);

		shader->setShiftSamplerBinding(baseSamplerBinding[compUnit.stage]);
		shader->setShiftTextureBinding(baseTextureBinding[compUnit.stage]);
		shader->setShiftImageBinding(baseImageBinding[compUnit.stage]);
		shader->setShiftUboBinding(baseUboBinding[compUnit.stage]);
		shader->setShiftSsboBinding(baseSsboBinding[compUnit.stage]);
		shader->setFlattenUniformArrays((Options & EOptionFlattenUniformArrays) != 0);
		shader->setNoStorageFormat((Options & EOptionNoStorageFormat) != 0);
		shader->setPreamble(defines);

		if (Options & EOptionAutoMapBindings)
			shader->setAutoMapBindings(true);

		shaders.push_back(shader);

		const int defaultVersion = Options & EOptionDefaultDesktop ? 110 : 100;

		if (Options & EOptionOutputPreprocessed) {
			std::string str;
			//glslang::TShader::ForbidIncluder includer;
			if (shader->preprocess(&Resources, defaultVersion, ENoProfile, false, false,
				messages, &str, includer)) {
				PutsIfNonEmpty(str.c_str());
			}
			else {
				CompileFailed = true;
			}
			StderrIfNonEmpty(shader->getInfoLog());
			StderrIfNonEmpty(shader->getInfoDebugLog());
			continue;
		}
		if (!shader->parse(&Resources, defaultVersion, ENoProfile, false, false, messages, includer))
			CompileFailed = true;

		program.addShader(shader);

		if (!(Options & EOptionSuppressInfolog) &&
			!(Options & EOptionMemoryLeakMode)) {
			//PutsIfNonEmpty(compUnit.fileName.c_str());
			PutsIfNonEmpty(shader->getInfoLog());
			PutsIfNonEmpty(shader->getInfoDebugLog());
		}
	}

	//
	// Program-level processing...
	//

	// Link
	if (!(Options & EOptionOutputPreprocessed) && !program.link(messages))
		LinkFailed = true;

	// Map IO
	if (Options & EOptionSpv) {
		if (!program.mapIO())
			LinkFailed = true;
	}

	// Report
	if (!(Options & EOptionSuppressInfolog) &&
		!(Options & EOptionMemoryLeakMode)) {
		PutsIfNonEmpty(program.getInfoLog());
		PutsIfNonEmpty(program.getInfoDebugLog());
	}

	// Reflect
	if (Options & EOptionDumpReflection) {
		program.buildReflection();
		program.dumpReflection();
	}

	// Dump SPIR-V
	if (Options & EOptionSpv) {
		if (CompileFailed || LinkFailed)
			printf("SPIR-V is not generated for failed compile or link\n");
		else {
			for (int stage = 0; stage < EShLangCount; ++stage) {
				if (program.getIntermediate((EShLanguage)stage)) {
					std::vector<uint32_t> spirv;
					std::string warningsErrors;
					spv::SpvBuildLogger logger;
					glslang::GlslangToSpv(*program.getIntermediate((EShLanguage)stage), spirv, &logger);

					if (outputSpirv) {
						std::string filename = std::string(tempdir) + "/" + removeExtension(extractFilename(sourcefilename)) + ".spirv";
						writeSpirv(filename.c_str(), spirv);
					}

					preprocessSpirv(spirv);

					static bool firstRun = true;
					if (!quiet && firstRun) {
						krafix::VarListTranslator* varPrinter = new krafix::VarListTranslator(spirv, shLanguageToShaderStage((EShLanguage)stage));
						varPrinter->print();
						firstRun = false;
					}

					krafix::Translator* translator = NULL;
					std::map<std::string, int> attributes;
					switch (target.lang) {
					case krafix::SpirV:
						translator = new krafix::SpirVTranslator(spirv, shLanguageToShaderStage((EShLanguage)stage));
						break;
					case krafix::GLSL:
						translator = new krafix::GlslTranslator2(spirv, shLanguageToShaderStage((EShLanguage)stage), relax);
						break;
					case krafix::HLSL:
						translator = new krafix::HlslTranslator2(spirv, shLanguageToShaderStage((EShLanguage)stage));
						break;
					case krafix::Metal:
						translator = new krafix::MetalTranslator2(spirv, shLanguageToShaderStage((EShLanguage)stage));
						break;
					case krafix::AGAL:
						translator = new krafix::AgalTranslator(spirv, shLanguageToShaderStage((EShLanguage)stage));
						break;
					case krafix::VarList:
						translator = new krafix::VarListTranslator(spirv, shLanguageToShaderStage((EShLanguage)stage));
						break;
					case krafix::JavaScript:
						translator = new krafix::JavaScriptTranslator2(spirv, shLanguageToShaderStage((EShLanguage)stage));
						break;
					}

					try {
						if (target.lang == krafix::HLSL && target.system != krafix::Unity) {
							std::string temp = tempdir == nullptr ? "" : std::string(tempdir) + "/" + removeExtension(extractFilename(sourcefilename)) + ".hlsl";
							char* tempoutput = nullptr;
							if (output) {
								tempoutput = new char[1024 * 1024];
							}
							translator->outputCode(target, sourcefilename, temp.c_str(), tempoutput, attributes);
							int returnCode = 0;
							if (target.version == 9) {
								returnCode = compileHLSLToD3D9(temp.c_str(), filename, tempoutput, output, length, attributes, (EShLanguage)stage);
							}
							else {
								returnCode = compileHLSLToD3D11(temp.c_str(), filename, tempoutput, output, length, attributes, (EShLanguage)stage, debugMode);
							}
							if (returnCode != 0) CompileFailed = true;
							delete[] tempoutput;
						}
						else if (target.lang == krafix::SpirV) {
							translator->outputCode(target, sourcefilename, filename, output, attributes);
							if (output != nullptr) {
								*length = dynamic_cast<krafix::SpirVTranslator*>(translator)->outputLength;
							}
						}
						else {
							translator->outputCode(target, sourcefilename, filename, output, attributes);
							if (output != nullptr) {
								*length = (int)strlen(output);
							}
						}
					}
					catch (spirv_cross::CompilerError& error) {
						printf("Error compiling to %s: %s\n", target.string().c_str(), error.what());
						CompileFailed = true;
					}

					delete translator;

					//glslang::OutputSpv(spirv, GetBinaryName((EShLanguage)stage));
					if (Options & EOptionHumanReadableSpv) {
						spv::Parameterize();
						spv::Disassemble(std::cout, spirv);
					}
				}
			}
		}
	}

	// Free everything up, program has to go before the shaders
	// because it might have merged stuff from the shaders, and
	// the stuff from the shaders has to have its destructors called
	// before the pools holding the memory in the shaders is freed.
	delete& program;
	while (shaders.size() > 0) {
		delete shaders.back();
		shaders.pop_back();
	}
}

krafix::TargetSystem getSystem(const char* system) {
	if (strcmp(system, "windows") == 0) return krafix::Windows;
	if (strcmp(system, "windowsapp") == 0) return krafix::WindowsApp;
	if (strcmp(system, "osx") == 0 || strcmp(system, "macos") == 0) return krafix::OSX;
	if (strcmp(system, "linux") == 0 || strcmp(system, "freebsd") == 0) return krafix::Linux;
	if (strcmp(system, "ios") == 0) return krafix::iOS;
	if (strcmp(system, "android") == 0) return krafix::Android;
	if (strcmp(system, "html5") == 0) return krafix::HTML5;
	if (strcmp(system, "debug-html5") == 0) return krafix::HTML5;
	if (strcmp(system, "html5worker") == 0) return krafix::HTML5;
	if (strcmp(system, "flash") == 0) return krafix::Flash;
	if (strcmp(system, "unity") == 0) return krafix::Unity;
	return krafix::Unknown;
}

//
// Do file IO part of compile and link, handing off the pure
// API/programmatic mode to CompileAndLinkShaderUnits(), which can
// be put in a loop for testing memory footprint and performance.
//
// This is just for linking mode: meaning all the shaders will be put into the
// the same program linked together.
//
// This means there are a limited number of work items (not multi-threading mode)
// and that the point is testing at the linking level. Hence, to enable
// performance and memory testing, the actual compile/link can be put in
// a loop, independent of processing the work items and file IO.
//
void CompileAndLinkShaderFiles(krafix::Target target, const char* sourcefilename, const char* filename, const char* tempdir, const char* source, char* output, int* length, glslang::TShader::Includer& includer, const char* defines, bool relax)
{
	std::vector<ShaderCompUnit> compUnits;

	// Transfer all the work items from to a simple list of
	// of compilation units.  (We don't care about the thread
	// work-item distribution properties in this path, which
	// is okay due to the limited number of shaders, know since
	// they are all getting linked together.)

	char* sources[] = { (char*)source, nullptr, nullptr, nullptr, nullptr };

	glslang::TWorkItem* workItem;
	while (Worklist.remove(workItem)) {
		ShaderCompUnit compUnit(
			FindLanguage(workItem->name),
			workItem->name,
			source != nullptr ? sources : ReadFileData(workItem->name.c_str())
		);

		if (!compUnit.text) {
			usage();
			return;
		}

		compUnits.push_back(compUnit);
	}

	// Actual call to programmatic processing of compile and link,
	// in a loop for testing memory and performance.  This part contains
	// all the perf/memory that a programmatic consumer will care about.
	for (int i = 0; i < ((Options & EOptionMemoryLeakMode) ? 100 : 1); ++i) {
		for (int j = 0; j < ((Options & EOptionMemoryLeakMode) ? 100 : 1); ++j)
			CompileAndLinkShaderUnits(compUnits, target, sourcefilename, filename, tempdir, output, length, includer, defines, relax);

		if (Options & EOptionMemoryLeakMode)
			glslang::OS_DumpMemoryCounters();
	}

	if (source == nullptr) {
		for (auto it = compUnits.begin(); it != compUnits.end(); ++it)
			FreeFileData(it->text);
	}
}

int compile(const char* targetlang, const char* from, std::string to, const char* tempdir, const char* source, char* output, int* length, const char* system,
	glslang::TShader::Includer& includer, std::string defines, int version, bool relax) {
	CompileFailed = false;

	//Options |= EOptionHumanReadableSpv;
	Options |= EOptionSpv;
	Options |= EOptionLinkProgram;
	//Options |= EOptionSuppressInfolog;

	NumWorkItems = 1;
	Work = new glslang::TWorkItem * [NumWorkItems];
	Work[0] = 0;

	if (from) {
		std::string name(from);
		if (!SetConfigFile(name)) {
			Work[0] = new glslang::TWorkItem(name);
			Worklist.add(Work[0]);
		}
	}
	else {
		std::string name = std::string("nothing.") + to;
		Work[0] = new glslang::TWorkItem(name);
		Worklist.add(Work[0]);
	}

	glslang::InitializeProcess();

	krafix::Target target;
	target.system = getSystem(system);
	target.es = false;
	if (strcmp(targetlang, "spirv") == 0) {
		target.lang = krafix::SpirV;
		target.version = version > 0 ? version : 1;
		defines += "#define SPIRV " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "d3d9") == 0) {
		target.lang = krafix::HLSL;
		target.version = version > 0 ? version : 9;
		defines += "#define HLSL " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "d3d11") == 0) {
		target.lang = krafix::HLSL;
		target.version = version > 0 ? version : 11;
		defines += "#define HLSL " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "glsl") == 0) {
		target.lang = krafix::GLSL;
		if (target.system == krafix::Linux && (FindLanguage(from) == EShLangVertex || FindLanguage(from) == EShLangFragment)) target.version = version > 0 ? version : 110;
		else target.version = version > 0 ? version : 330;
		defines += "#define GLSL " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "essl") == 0) {
		target.lang = krafix::GLSL;
		if (FindLanguage(from) == EShLangVertex || FindLanguage(from) == EShLangFragment) target.version = version > 0 ? version : 100;
		else target.version = version > 0 ? version : 310;
		target.es = true;
		defines += "#define GLSL " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "agal") == 0) {
		target.lang = krafix::AGAL;
		target.version = version > 0 ? version : 100;
		target.es = true;
		defines += "#define AGAL " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "metal") == 0) {
		target.lang = krafix::Metal;
		target.version = version > 0 ? version : 1;
		defines += "#define METAL " + std::to_string(target.version) + "\n";
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "varlist") == 0) {
		target.lang = krafix::VarList;
		target.version = version > 0 ? version : 1;
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else if (strcmp(targetlang, "js") == 0 || strcmp(targetlang, "javascript") == 0) {
		target.lang = krafix::JavaScript;
		target.version = version > 0 ? version : 1;
		CompileAndLinkShaderFiles(target, from, to.c_str(), tempdir, source, output, length, includer, defines.c_str(), relax);
	}
	else {
		std::cout << "Unknown profile " << targetlang << std::endl;
		CompileFailed = true;
	}
	if (!CompileFailed && !quiet) {
		std::cerr << "#file:" << to << std::endl;
	}

	glslang::FinalizeProcess();

	if (CompileFailed || LinkFailed) return 1;
	else return 0;
}

int compileOptionallyRelaxed(const char* targetlang, const char* from, std::string to, std::string ext, const char* tempdir, const char* source, char* output, int* length, const char* system,
	glslang::TShader::Includer& includer, std::string defines, int version, bool relax) {
	int regularErrors = 0, relaxErrors = 0, es3Errors = 0;

	if (strcmp(system, "html5") == 0 || strcmp(system, "debug-html5") == 0 || strcmp(system, "html5worker") == 0 || strcmp(system, "emscripten") == 0 || strcmp(system, "wasm") == 0) {
		if (version >= 300) { // -webgl2 only
			es3Errors = compile(targetlang, from, to + "-webgl2" + ext, tempdir, source, output, length, system, includer, defines, 300, false);
			return es3Errors;
		}
		else {
			regularErrors = compile(targetlang, from, to + ext, tempdir, source, output, length, system, includer, defines, version, false);
			es3Errors = compile(targetlang, from, to + "-webgl2" + ext, tempdir, source, output, length, system, includer, defines, 300, false);
			if (relax) {
				relaxErrors = compile(targetlang, from, to + "-relaxed" + ext, tempdir, source, output, length, system, includer, defines, version, true);
				return std::min(regularErrors, std::min(relaxErrors, es3Errors));
			}
			else {
				return std::min(regularErrors, es3Errors);
			}
		}
	}
	else {
		regularErrors = compile(targetlang, from, to + ext, tempdir, source, output, length, system, includer, defines, version, false);
		if (relax) {
			relaxErrors = compile(targetlang, from, to + "-relaxed" + ext, tempdir, source, output, length, system, includer, defines, version, true);
			return std::min(regularErrors, relaxErrors);
		}
		else {
			return regularErrors;
		}
	}
}

int compileOptionallyInstanced(const char* targetlang, const char* from, std::string to, std::string ext, const char* tempdir, const char* source, char* output, int* length, const char* system,
	glslang::TShader::Includer& includer, std::string defines, int version, bool instanced, bool relax) {
	int errors = 0;
	if (instanced) {
		errors += compileOptionallyRelaxed(targetlang, from, to + "-noinst", ext, tempdir, source, output, length, system, includer, defines, version, relax);
		errors += compileOptionallyRelaxed(targetlang, from, to + "-inst", ext, tempdir, source, output, length, system, includer, defines + "#define INSTANCED_RENDERING\n", version, relax);
	}
	else {
		errors += compileOptionallyRelaxed(targetlang, from, to, ext, tempdir, source, output, length, system, includer, defines, version, relax);
	}
	return errors;
}

int compileWithTextureUnits(const char* targetlang, const char* from, std::string to, std::string ext, const char* tempdir, const char* source, char* output, int* length, const char* system,
	glslang::TShader::Includer& includer, std::string defines, int version, const std::vector<int>& textureUnitCounts, bool usesTextureUnitsCount, bool instanced, bool relax) {
	int errors = 0;
	if (usesTextureUnitsCount && textureUnitCounts.size() > 0) {
		for (size_t i = 0; i < textureUnitCounts.size(); ++i) {
			int texcount = textureUnitCounts[i];
			std::stringstream toto;
			toto << to << "-tex" << texcount << ext;
			std::stringstream definesplustex;
			definesplustex << defines << "#define MAX_TEXTURE_UNITS=" << texcount << "\n";
			errors += compileOptionallyInstanced(targetlang, from, toto.str(), ext, tempdir, source, output, length, system, includer, definesplustex.str(), version, instanced, relax);
		}
	}
	else {
		errors += compileOptionallyInstanced(targetlang, from, to, ext, tempdir, source, output, length, system, includer, defines, version, instanced, relax);
	}
	return errors;
}

extern "C" int krafix_compile(const char* source, char* output, int* length, const char* targetlang, const char* system, const char* shadertype, int version) {
	// Reset fail states
	CompileFailed = false;
	LinkFailed = false;

	std::string defines;
	std::vector<int> textureUnitCounts;
	bool instancedoptional = false;
	// int version = -1;
	bool getversion = false;
	bool relax = false;

	//defines += "#define " + arg.substr(2) + "\n";
	//textureUnitCounts.push_back(atoi(arg.substr(2).c_str()));
	//instancedoptional = true;
	//debugMode = true;
	//relax = true;
	quiet = true;

	ProcessConfigFile();

	//glslang::InitializeProcess();

	NullIncluder includer;

	bool usesTextureUnitsCount = false;
	bool usesInstancedoptional = false;

	/*if (textureUnitCounts.size() > 0 || instancedoptional) {
		std::stringstream filecontentstream;
		std::string line;
		std::ifstream file(from);
		if (file.is_open()) {
			while (getline(file, line)) {
				filecontentstream << line << '\n';
			}
			file.close();
		}
		std::string filecontent = filecontentstream.str();

		if (filecontent.find("MAX_TEXTURE_UNITS") != std::string::npos) {
			usesTextureUnitsCount = true;
		}
		if (filecontent.find("INSTANCED_RENDERING") != std::string::npos) {
			usesInstancedoptional = true;
		}
	}*/

	char from[256];
	strcpy(from, ".");
	strcat(from, shadertype);
	strcat(from, ".glsl");

	return compileWithTextureUnits(targetlang, from, "", shadertype, nullptr, source, output, length, system, includer, defines, version, textureUnitCounts, usesTextureUnitsCount, instancedoptional && usesInstancedoptional, relax);
}

// d3d11 in/basic.vert.glsl test.d3d11 temp windows
#ifndef KRAFIX_LIBRARY
int C_DECL main(int argc, char* argv[]) {
	if (argc < 6) {
		usage();
		return 1;
	}

	const char* tempdir = argv[4];

	std::vector<std::string> allOptions;

	std::string defines;
	std::vector<int> textureUnitCounts;
	bool instancedoptional = false;
	int version = -1;
	bool getversion = false;
	bool getDependencyFileLocation = false;
	std::string dependencyFileLocation;
	bool relax = false;

	for (int i = 6; i < argc; ++i) {
		std::string arg = argv[i];
		if (getversion) {
			version = atoi(argv[i]);
			getversion = false;
			allOptions.push_back(std::string("version: ") + argv[i]);
		}
		else if (getDependencyFileLocation) {
			dependencyFileLocation = argv[i];
			getDependencyFileLocation = false;
			deps = true;
		}
		else if (arg.substr(0, 2) == "-D") {
			defines += "#define " + arg.substr(2) + "\n";
			allOptions.push_back(std::string("define: ") + arg.substr(2));
		}
		else if (arg.substr(0, 2) == "-T") {
			textureUnitCounts.push_back(atoi(arg.substr(2).c_str()));
			allOptions.push_back(std::string("TextureUnitCount: " + arg.substr(2)));
		}
		else if (arg == "--instancedoptional") {
			instancedoptional = true;
			allOptions.push_back("instancedoptional");
		}
		else if (arg == "--debug") {
			debugMode = true;
			allOptions.push_back("debug");
		}
		else if (arg == "--version") {
			getversion = true;
		}
		else if (arg == "--quiet") {
			quiet = true;
		}
		else if (arg == "--relax") {
			relax = true;
			allOptions.push_back("relax");
		}
		else if (arg == "--deps") {
			getDependencyFileLocation = true;
		}
		else if (arg == "--outputintermediatespirv") {
			outputSpirv = true;
		}
	}

	const char* targetlang = argv[1];
	const char* from = argv[2];
	std::string to = argv[3];
	const char* system = argv[5];

	ProcessConfigFile();

	//glslang::InitializeProcess();

	KrafixIncluder includer(from);

	if (deps) {
		dependencies.push_back(argv[0]);
	}

	bool usesTextureUnitsCount = false;
	bool usesInstancedoptional = false;

	if (textureUnitCounts.size() > 0 || instancedoptional) {
		std::stringstream filecontentstream;
		std::string line;
		std::ifstream file(from);
		if (file.is_open()) {
			while (getline(file, line)) {
				filecontentstream << line << '\n';
			}
			file.close();
		}
		std::string filecontent = filecontentstream.str();

		if (filecontent.find("MAX_TEXTURE_UNITS") != std::string::npos) {
			usesTextureUnitsCount = true;
		}
		if (filecontent.find("INSTANCED_RENDERING") != std::string::npos) {
			usesInstancedoptional = true;
		}
	}

	size_t split1 = to.find_last_of('/');
	size_t split2 = to.find_last_of('\\');
	size_t split;
	if (split1 == std::string::npos && split2 == std::string::npos) {
		split = 0;
	}
	else if (split1 == std::string::npos || split2 == std::string::npos) {
		split = std::min(split1, split2);
	}
	else {
		split = std::max(split1, split2);
	}
	std::string towithoutext = to.substr(0, to.find_first_of('.', split));
	std::string ext = to.substr(to.find_first_of('.', split));

	int errors = 0;
	if (strcmp(targetlang, "varlist") == 0) {
		int length = 0;
		compile(targetlang, from, to, tempdir, nullptr, nullptr, &length, system, includer, defines, version, false);
		if (CompileFailed || LinkFailed) ++errors;
	}
	else {
		int length = 0;
		errors = compileWithTextureUnits(targetlang, from, towithoutext, ext, tempdir, nullptr, nullptr, &length, system, includer, defines, version, textureUnitCounts, usesTextureUnitsCount, instancedoptional && usesInstancedoptional, relax);
	}

	if (deps && errors == 0) {
		std::ofstream out;
		out.open(dependencyFileLocation, std::ios::binary | std::ios::out);

		for (int i = 0; i < allOptions.size(); ++i) {
			out << allOptions[i] << "\n";
		}

		out << "--\n";

		for (int i = 0; i < dependencies.size(); ++i) {
			out << dependencies[i] << "\n";
		}

		out.close();
	}

	return errors;
}
#endif

//
//   Deduce the language from the filename.  Files must end in one of the
//   following extensions:
//
//   .vert = vertex
//   .tesc = tessellation control
//   .tese = tessellation evaluation
//   .geom = geometry
//   .frag = fragment
//   .comp = compute
//
EShLanguage FindLanguage(const std::string& name, bool parseSuffix)
{
	size_t ext = 0;
	std::string suffix;

	if (shaderStageName)
		suffix = shaderStageName;
	else {
		// Search for a suffix on a filename: e.g, "myfile.frag".  If given
		// the suffix directly, we skip looking for the '.'
		if (parseSuffix) {
			ext = name.rfind('.');
			if (ext == std::string::npos) {
				usage();
				return EShLangVertex;
			}
			++ext;
		}
		suffix = name.substr(ext, std::string::npos);
	}

	if (suffix == "glsl") {
		size_t ext2 = name.substr(0, ext - 1).rfind('.');
		suffix = name.substr(ext2 + 1, ext - ext2 - 2);
	}

	if (suffix == "vert")
		return EShLangVertex;
	else if (suffix == "tesc")
		return EShLangTessControl;
	else if (suffix == "tese")
		return EShLangTessEvaluation;
	else if (suffix == "geom")
		return EShLangGeometry;
	else if (suffix == "frag")
		return EShLangFragment;
	else if (suffix == "comp")
		return EShLangCompute;

	usage();
	return EShLangVertex;
}

//
// Read a file's data into a string, and compile it using the old interface ShCompile,
// for non-linkable results.
//
void CompileFile(const char* fileName, ShHandle compiler)
{
	int ret = 0;
	char** shaderStrings = ReadFileData(fileName);
	if (!shaderStrings) {
		usage();
	}

	int* lengths = new int[NumShaderStrings];

	// move to length-based strings, rather than null-terminated strings
	for (int s = 0; s < NumShaderStrings; ++s)
		lengths[s] = (int)strlen(shaderStrings[s]);

	if (!shaderStrings) {
		CompileFailed = true;
		return;
	}

	EShMessages messages = EShMsgDefault;
	SetMessageOptions(messages);

	for (int i = 0; i < ((Options & EOptionMemoryLeakMode) ? 100 : 1); ++i) {
		for (int j = 0; j < ((Options & EOptionMemoryLeakMode) ? 100 : 1); ++j) {
			// ret = ShCompile(compiler, shaderStrings, NumShaderStrings, lengths, EShOptNone, &Resources, Options, (Options & EOptionDefaultDesktop) ? 110 : 100, false, messages);
			ret = ShCompile(compiler, shaderStrings, NumShaderStrings, nullptr, EShOptNone, &Resources, Options, (Options & EOptionDefaultDesktop) ? 110 : 100, false, messages);
			// const char* multi[12] = { "# ve", "rsion", " 300 e", "s", "\n#err",
			//                         "or should be l", "ine 1", "string 5\n", "float glo", "bal",
			//                         ";\n#error should be line 2\n void main() {", "global = 2.3;}" };
			// const char* multi[7] = { "/", "/", "\\", "\n", "\n", "#", "version 300 es" };
			// ret = ShCompile(compiler, multi, 7, nullptr, EShOptNone, &Resources, Options, (Options & EOptionDefaultDesktop) ? 110 : 100, false, messages);
		}

		if (Options & EOptionMemoryLeakMode)
			glslang::OS_DumpMemoryCounters();
	}

	delete[] lengths;
	FreeFileData(shaderStrings);

	if (ret == 0)
		CompileFailed = true;
}

//
//   print usage to stdout
//
void usage()
{
	printf("Usage: krafix profile in out tempdir system\n");

	/*printf("Usage: glslangValidator [option]... [file]...\n"
		   "\n"
		   "Where: each 'file' ends in .<stage>, where <stage> is one of\n"
		   "    .conf   to provide an optional config file that replaces the default configuration\n"
		   "            (see -c option below for generating a template)\n"
		   "    .vert   for a vertex shader\n"
		   "    .tesc   for a tessellation control shader\n"
		   "    .tese   for a tessellation evaluation shader\n"
		   "    .geom   for a geometry shader\n"
		   "    .frag   for a fragment shader\n"
		   "    .comp   for a compute shader\n"
		   "\n"
		   "Compilation warnings and errors will be printed to stdout.\n"
		   "\n"
		   "To get other information, use one of the following options:\n"
		   "Each option must be specified separately.\n"
		   "  -V          create SPIR-V binary, under Vulkan semantics; turns on -l;\n"
		   "              default file name is <stage>.spv (-o overrides this)\n"
		   "  -G          create SPIR-V binary, under OpenGL semantics; turns on -l;\n"
		   "              default file name is <stage>.spv (-o overrides this)\n"
		   "  -H          print human readable form of SPIR-V; turns on -V\n"
		   "  -E          print pre-processed GLSL; cannot be used with -l;\n"
		   "              errors will appear on stderr.\n"
		   "  -S <stage>  uses specified stage rather than parsing the file extension\n"
		   "              valid choices for <stage> are vert, tesc, tese, geom, frag, or comp\n"
		   "  -c          configuration dump;\n"
		   "              creates the default configuration file (redirect to a .conf file)\n"
		   "  -C          cascading errors; risks crashes from accumulation of error recoveries\n"
		   "  -d          default to desktop (#version 110) when there is no shader #version\n"
		   "              (default is ES version 100)\n"
		   "  -D          input is HLSL\n"
		   "  -e          specify entry-point name\n"
		   "  -h          print this usage message\n"
		   "  -i          intermediate tree (glslang AST) is printed out\n"
		   "  -l          link all input files together to form a single module\n"
		   "  -m          memory leak mode\n"
		   "  -o  <file>  save binary to <file>, requires a binary option (e.g., -V)\n"
		   "  -q          dump reflection query database\n"
		   "  -r          relaxed semantic error-checking mode\n"
		   "  -s          silent mode\n"
		   "  -t          multi-threaded mode\n"
		   "  -v          print version strings\n"
		   "  -w          suppress warnings (except as required by #extension : warn)\n"
		   "  -x          save 32-bit hexadecimal numbers as text, requires a binary option (e.g., -V)\n"
		   "\n"
		   "  --shift-sampler-binding [stage] num     set base binding number for samplers\n"
		   "  --ssb [stage] num                       synonym for --shift-sampler-binding\n"
		   "\n"
		   "  --shift-texture-binding [stage] num     set base binding number for textures\n"
		   "  --stb [stage] num                       synonym for --shift-texture-binding\n"
		   "\n"
		   "  --shift-image-binding [stage] num       set base binding number for images (uav)\n"
		   "  --sib [stage] num                       synonym for --shift-image-binding\n"
		   "\n"
		   "  --shift-UBO-binding [stage] num         set base binding number for UBOs\n"
		   "  --sub [stage] num                       synonym for --shift-UBO-binding\n"
		   "\n"
		   "  --shift-ssbo-binding [stage] num        set base binding number for SSBOs\n"
		   "  --sbb [stage] num                       synonym for --shift-ssbo-binding\n"
		   "\n"
		   "  --auto-map-bindings                     automatically bind uniform variables without\n"
		   "                                          explicit bindings.\n"
		   "  --amb                                   synonym for --auto-map-bindings\n"
		   "\n"
		   "  --flatten-uniform-arrays                flatten uniform texture & sampler arrays to scalars\n"
		   "  --fua                                   synonym for --flatten-uniform-arrays\n"
		   "\n"
		   "  --no-storage-format                     use Unknown image format\n"
		   "  --nsf                                   synonym for --no-storage-format\n"
		   "\n"
		   "  --source-entrypoint name                the given shader source function is renamed to be the entry point given in -e\n"
		   "  --sep                                   synonym for --source-entrypoint\n"
		   "\n"
		   "  --keep-uncalled                         don't eliminate uncalled functions when linking\n"
		   "  --ku                                    synonym for --keep-uncalled\n"
		   "  --variable-name <name>                  Creates a C header file that contains a uint32_t array named <name> initialized with the shader binary code.\n"
		   "  --vn <name>                             synonym for --variable-name <name>.\n"
		   );*/

	exit(EFailUsage);
}

#if !defined _MSC_VER && !defined MINGW_HAS_SECURE_API

#include <errno.h>

int fopen_s(
	FILE** pFile,
	const char* filename,
	const char* mode
)
{
	if (!pFile || !filename || !mode) {
		return EINVAL;
	}

	FILE* f = fopen(filename, mode);
	if (!f) {
		if (errno != 0) {
			return errno;
		}
		else {
			return ENOENT;
		}
	}
	*pFile = f;

	return 0;
}

#endif

//
//   Malloc a string of sufficient size and read a string into it.
//
char** ReadFileData(const char* fileName)
{
	FILE* in = nullptr;
	int errorCode = fopen_s(&in, fileName, "r");

	int count = 0;
	const int maxSourceStrings = 5;  // for testing splitting shader/tokens across multiple strings
	char** return_data = (char**)malloc(sizeof(char*) * (maxSourceStrings + 1)); // freed in FreeFileData()

	if (errorCode || in == nullptr)
		Error("unable to open input file");

	while (fgetc(in) != EOF)
		count++;

	fseek(in, 0, SEEK_SET);

	char* fdata = (char*)malloc(count + 2); // freed before return of this function
	if (!fdata)
		Error("can't allocate memory");

	if ((int)fread(fdata, 1, count, in) != count) {
		free(fdata);
		Error("can't read input file");
	}

	fdata[count] = '\0';
	fclose(in);

	if (count == 0) {
		// recover from empty file
		return_data[0] = (char*)malloc(count + 2);  // freed in FreeFileData()
		return_data[0][0] = '\0';
		NumShaderStrings = 0;
		free(fdata);

		return return_data;
	}
	else
		NumShaderStrings = 1;  // Set to larger than 1 for testing multiple strings

	// compute how to split up the file into multiple strings, for testing multiple strings
	int len = (int)(ceil)((float)count / (float)NumShaderStrings);
	int ptr_len = 0;
	int i = 0;
	while (count > 0) {
		return_data[i] = (char*)malloc(len + 2);  // freed in FreeFileData()
		memcpy(return_data[i], fdata + ptr_len, len);
		return_data[i][len] = '\0';
		count -= len;
		ptr_len += len;
		if (count < len) {
			if (count == 0) {
				NumShaderStrings = i + 1;
				break;
			}
			len = count;
		}
		++i;
	}

	free(fdata);

	return return_data;
}

void FreeFileData(char** data)
{
	for (int i = 0; i < NumShaderStrings; i++)
		free(data[i]);

	free(data);
}

void InfoLogMsg(const char* msg, const char* name, const int num)
{
	if (num >= 0)
		printf("#### %s %s %d INFO LOG ####\n", msg, name, num);
	else
		printf("#### %s %s INFO LOG ####\n", msg, name);
}

#ifdef _WIN32
#include <Windows.h>
#endif

void executeSync(const char* command) {
#ifdef _WIN32
	STARTUPINFOA startupInfo;
	PROCESS_INFORMATION processInfo;
	memset(&startupInfo, 0, sizeof(startupInfo));
	memset(&processInfo, 0, sizeof(processInfo));
	startupInfo.cb = sizeof(startupInfo);
	CreateProcessA(nullptr, (char*)command, nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE, "PATH=%PATH%;.\\cygwin\\bin\0", nullptr, &startupInfo, &processInfo);
	WaitForSingleObject(processInfo.hProcess, INFINITE);
	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
#else
	system(command);
#endif
}
