"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
require("source-map-support/register");
const path = require("path");
const ts = require("typescript");
const readline = require("readline");
if (process.argv.length < 3) {
    throw new Error('No project directory specified');
}
const projectDirectory = path.resolve(process.argv[2]);
const responseCache = new Map();
const scriptFileNames = new Array();
const scriptVersions = new Map();
const fileCache = new Map();
const compilerOptions = findCompilerOptions();
const languageService = createLanguageService();
function findCompilerOptions() {
    const configPath = ts.findConfigFile(projectDirectory, ts.sys.fileExists);
    if (!configPath) {
        throw new Error(`Failed to find tsconfig in: ${projectDirectory}`);
    }
    const configJson = ts.readConfigFile(configPath, ts.sys.readFile);
    if (configJson.error) {
        throw new Error(formatDiagnostic(configJson.error));
    }
    const config = ts.parseJsonConfigFileContent(configJson.config, {
        readDirectory: ts.sys.readDirectory,
        fileExists: ts.sys.fileExists,
        readFile: ts.sys.readFile,
        useCaseSensitiveFileNames: false
    }, projectDirectory);
    if (config.errors.length) {
        throw new Error(config.errors.map(error => (formatDiagnostic(error))).join('\n'));
    }
    return config.options;
}
function formatDiagnostic(diagnostic) {
    const msg = ts.flattenDiagnosticMessageText(diagnostic.messageText, '\n');
    if (!diagnostic.file) {
        return `[TS]: ${msg}`;
    }
    const pos = diagnostic.file.getLineAndCharacterOfPosition(diagnostic.start || 0);
    const file = path.basename(diagnostic.file.fileName);
    const line = pos.line + 1;
    const char = pos.character + 1;
    const category = diagnostic.category;
    const type = ts.DiagnosticCategory[category].toLowerCase();
    const code = diagnostic.code;
    return `[TS]: ${file}(${line},${char}): ${type} TS${code}: ${msg}`;
}
function createLanguageService() {
    return ts.createLanguageService({
        getScriptFileNames: () => scriptFileNames,
        getScriptVersion: fileName => {
            const scriptVersion = scriptVersions.get(fileName);
            return scriptVersion ? scriptVersion.version.toString() : '0';
        },
        getScriptSnapshot: fileName => {
            const cachedSnapshot = fileCache.get(fileName);
            if (cachedSnapshot) {
                return cachedSnapshot;
            }
            const fileContent = ts.sys.readFile(fileName);
            if (fileContent === undefined) {
                scriptVersions.delete(fileName);
                return undefined;
            }
            const snapshot = ts.ScriptSnapshot.fromString(fileContent);
            if (fileName.search(/node_modules/) !== -1) {
                fileCache.set(fileName, snapshot);
            }
            if (!scriptVersions.get(fileName)) {
                scriptVersions.set(fileName, {
                    version: 0,
                    modifiedTime: Date.now()
                });
            }
            const scriptVersion = scriptVersions.get(fileName);
            if (!scriptVersion) {
                return undefined;
            }
            const modifiedTime = getModifiedTime(fileName);
            if (modifiedTime > scriptVersion.modifiedTime) {
                scriptVersion.modifiedTime = modifiedTime;
                scriptVersion.version += 1;
            }
            return snapshot;
        },
        getCurrentDirectory: () => projectDirectory,
        getCompilationSettings: () => compilerOptions,
        getDefaultLibFileName: ts.getDefaultLibFilePath,
        fileExists: ts.sys.fileExists,
        readFile: ts.sys.readFile,
        readDirectory: ts.sys.readDirectory
    }, ts.createDocumentRegistry());
}
function getModifiedTime(filePath) {
    const modifiedTime = ts.sys.getModifiedTime(filePath);
    if (!modifiedTime) {
        throw new Error(`Failed to get last modified time for: '${filePath}'`);
    }
    return modifiedTime.getTime();
}
function parseFile(filePath) {
    const modifiedTime = getModifiedTime(filePath);
    let scriptVersion = scriptVersions.get(filePath);
    if (!scriptVersion) {
        scriptVersion = {
            version: 0,
            modifiedTime: modifiedTime
        };
        scriptFileNames.push(filePath);
        scriptVersions.set(filePath, scriptVersion);
    }
    else {
        scriptVersion.version += 1;
    }
    const program = languageService.getProgram();
    if (program === undefined) {
        throw new Error('Failed to get program');
    }
    const sourceFile = program.getSourceFile(filePath);
    if (sourceFile === undefined) {
        throw new Error(`Failed to get source file: ${filePath}`);
    }
    const diagnostics = ts.getPreEmitDiagnostics(program, sourceFile);
    const errors = diagnostics.map(formatDiagnostic);
    if (errors.length > 0) {
        return { errors: errors };
    }
    const emitOutput = languageService.getEmitOutput(filePath);
    if (emitOutput.emitSkipped) {
        throw new Error('Emit skipped');
    }
    const numOutput = emitOutput.outputFiles.length;
    if (numOutput !== 1) {
        throw new Error(`Unexpected number of output files: ${numOutput}`);
    }
    scriptVersion.modifiedTime = modifiedTime;
    const output = emitOutput.outputFiles[0];
    const sources = new Map();
    sources.set(filePath, output.text);
    const fileExtension = path.parse(filePath).ext;
    const fileName = path.basename(filePath);
    const plainName = fileName.replace(fileExtension, '');
    const functions = (sourceFile.statements
        .filter(ts.isFunctionDeclaration)
        .filter(f => f.body !== undefined)
        .filter(isExported)
        .map(declaration => parseFunction(declaration, filePath, program, sourceFile, errors))
        .filter(isNonEmpty));
    const dependencies = findDependencies(program, sourceFile);
    return {
        fileName: fileName,
        name: plainName,
        path: filePath,
        source: sources.get(filePath) || '',
        errors: errors,
        exports: functions,
        dependencies: dependencies
    };
}
function isExported(node) {
    const modifiers = node.modifiers;
    return modifiers && modifiers.some(mod => mod.kind === ts.SyntaxKind.ExportKeyword);
}
function isNonEmpty(value) {
    return value != undefined;
}
function parseFunction(declaration, filePath, program, sourceFile, errors) {
    const typeChecker = program.getTypeChecker();
    const signature = typeChecker.getSignatureFromDeclaration(declaration);
    const returnType = typeChecker.getReturnTypeOfSignature(signature);
    const returnTypeStr = typeChecker.typeToString(returnType);
    const returnTypes = parseType(returnTypeStr);
    const position = getPosition(declaration, sourceFile);
    if (returnTypes.length > 1) {
        const file = path.basename(filePath);
        const line = position.line;
        const char = position.character;
        errors.push(`[TSU] ${file}(${line},${char}): ` +
            `Disallowed union return type (${returnTypeStr})`);
        return null;
    }
    const parameters = declaration.parameters.map(param => {
        return parseParameter(param, typeChecker);
    });
    return {
        name: declaration.name.getText(),
        parameters: parameters,
        returnTypes: returnTypes,
        line: position.line,
        character: position.character
    };
}
function parseParameter(param, typeChecker) {
    const type = typeChecker.getTypeAtLocation(param.type);
    const types = parseType(typeChecker.typeToString(type));
    const optional = (param.initializer === undefined
        ? typeChecker.isOptionalParameter(param)
        : false);
    return {
        name: param.name.getText(),
        types: types,
        optional: optional
    };
}
function parseType(typeStr) {
    return typeStr.split(' | ').map(str => {
        const name = str.replace(/\[\]/g, '');
        const dimensions = (str.match(/\[\]/g) || []).length;
        return {
            name: name,
            dimensions: dimensions
        };
    });
}
function getPosition(node, sourceFile) {
    const pos = sourceFile.getLineAndCharacterOfPosition(node.getStart(sourceFile, true));
    pos.line += 1;
    pos.character += 1;
    return pos;
}
function findDependencies(program, sourceFile) {
    const typeChecker = program.getTypeChecker();
    const isUObject = (type) => {
        const typeName = typeChecker.typeToString(type);
        if (typeName === 'UObject') {
            return true;
        }
        const baseTypes = type.getBaseTypes() || [];
        return !!baseTypes.find(isUObject);
    };
    const dependencies = new Set();
    const traverseTree = (node) => {
        ts.forEachChild(node, traverseTree);
        let type;
        try {
            type = typeChecker.getTypeAtLocation(node);
        }
        catch (_a) {
            return;
        }
        if (!type) {
            return;
        }
        const flagName = (ts.TypeFlags[type.flags] || '');
        const isLiteral = (flagName.search(/Literal/) !== -1);
        const isCallable = !type.getCallSignatures().length;
        const isValidNode = !isLiteral && isCallable;
        if (!isValidNode) {
            return;
        }
        const constructSignatures = type.getConstructSignatures();
        if (constructSignatures.length) {
            type = constructSignatures[0].getReturnType();
        }
        if (isUObject(type)) {
            const typeName = typeChecker.typeToString(type);
            dependencies.add(typeName);
        }
    };
    traverseTree(sourceFile);
    return Array.from(dependencies);
}
function writeResponse(response) {
    if (typeof response !== 'string') {
        response = JSON.stringify(response);
    }
    process.stdout.write('\nRESPONSE_TAG ' + response + '\n');
    return response;
}
function processRequest(requestStr) {
    const request = JSON.parse(requestStr);
    const cachedResponse = responseCache.get(request.file);
    if (cachedResponse) {
        writeResponse(cachedResponse);
        return;
    }
    var response;
    try {
        response = parseFile(request.file);
    }
    catch (err) {
        process.stdout.write(err.message + '\n');
        process.exit(-1);
        return;
    }
    const responseStr = writeResponse(response);
    responseCache.set(request.file, responseStr);
}
process.stdin.setEncoding("utf8");
var reader = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: false
});
reader.on('line', function (line) {
    var text = line.trim();
    console.log("<", text, ">");
    if (text == "EXIT") {
        console.log("end parse");
        process.exit(0);
        return;
    }
    if (text.length > 0) {
        processRequest(line);
    }
});
console.log("start parse");
//# sourceMappingURL=data:application/json;base64,eyJ2ZXJzaW9uIjozLCJmaWxlIjoiaW5kZXguanMiLCJzb3VyY2VSb290IjoiIiwic291cmNlcyI6WyIuLi9zb3VyY2UvaW5kZXgudHMiXSwibmFtZXMiOltdLCJtYXBwaW5ncyI6Ijs7QUFBQSx1Q0FBcUM7QUFFckMsNkJBQTZCO0FBQzdCLGlDQUFpQztBQUNqQyxxQ0FBcUM7QUE4Q3JDLElBQUksT0FBTyxDQUFDLElBQUksQ0FBQyxNQUFNLEdBQUcsQ0FBQyxFQUFFO0lBQzVCLE1BQU0sSUFBSSxLQUFLLENBQUMsZ0NBQWdDLENBQUMsQ0FBQztDQUNsRDtBQUVELE1BQU0sZ0JBQWdCLEdBQUcsSUFBSSxDQUFDLE9BQU8sQ0FBQyxPQUFPLENBQUMsSUFBSSxDQUFDLENBQUMsQ0FBQyxDQUFDLENBQUM7QUFDdkQsTUFBTSxhQUFhLEdBQUcsSUFBSSxHQUFHLEVBQWtCLENBQUM7QUFDaEQsTUFBTSxlQUFlLEdBQUcsSUFBSSxLQUFLLEVBQVUsQ0FBQztBQUM1QyxNQUFNLGNBQWMsR0FBRyxJQUFJLEdBQUcsRUFBeUIsQ0FBQztBQUN4RCxNQUFNLFNBQVMsR0FBRyxJQUFJLEdBQUcsRUFBOEIsQ0FBQztBQUN4RCxNQUFNLGVBQWUsR0FBRyxtQkFBbUIsRUFBRSxDQUFDO0FBQzlDLE1BQU0sZUFBZSxHQUFHLHFCQUFxQixFQUFFLENBQUM7QUFFaEQsU0FBUyxtQkFBbUI7SUFDM0IsTUFBTSxVQUFVLEdBQUcsRUFBRSxDQUFDLGNBQWMsQ0FBQyxnQkFBZ0IsRUFBRSxFQUFFLENBQUMsR0FBRyxDQUFDLFVBQVUsQ0FBQyxDQUFDO0lBQzFFLElBQUksQ0FBQyxVQUFVLEVBQUU7UUFDaEIsTUFBTSxJQUFJLEtBQUssQ0FBQywrQkFBK0IsZ0JBQWdCLEVBQUUsQ0FBQyxDQUFDO0tBQ25FO0lBRUQsTUFBTSxVQUFVLEdBQUcsRUFBRSxDQUFDLGNBQWMsQ0FBQyxVQUFVLEVBQUUsRUFBRSxDQUFDLEdBQUcsQ0FBQyxRQUFRLENBQUMsQ0FBQztJQUNsRSxJQUFJLFVBQVUsQ0FBQyxLQUFLLEVBQUU7UUFDckIsTUFBTSxJQUFJLEtBQUssQ0FBQyxnQkFBZ0IsQ0FBQyxVQUFVLENBQUMsS0FBSyxDQUFDLENBQUMsQ0FBQztLQUNwRDtJQUVELE1BQU0sTUFBTSxHQUFHLEVBQUUsQ0FBQywwQkFBMEIsQ0FDM0MsVUFBVSxDQUFDLE1BQU0sRUFDakI7UUFDQyxhQUFhLEVBQUUsRUFBRSxDQUFDLEdBQUcsQ0FBQyxhQUFhO1FBQ25DLFVBQVUsRUFBRSxFQUFFLENBQUMsR0FBRyxDQUFDLFVBQVU7UUFDN0IsUUFBUSxFQUFFLEVBQUUsQ0FBQyxHQUFHLENBQUMsUUFBUTtRQUN6Qix5QkFBeUIsRUFBRSxLQUFLO0tBQ2hDLEVBQ0QsZ0JBQWdCLENBQ2hCLENBQUM7SUFFRixJQUFJLE1BQU0sQ0FBQyxNQUFNLENBQUMsTUFBTSxFQUFFO1FBQ3pCLE1BQU0sSUFBSSxLQUFLLENBQUMsTUFBTSxDQUFDLE1BQU0sQ0FBQyxHQUFHLENBQUMsS0FBSyxDQUFDLEVBQUUsQ0FBQyxDQUMxQyxnQkFBZ0IsQ0FBQyxLQUFLLENBQUMsQ0FDdkIsQ0FBQyxDQUFDLElBQUksQ0FBQyxJQUFJLENBQUMsQ0FBQyxDQUFDO0tBQ2Y7SUFFRCxPQUFPLE1BQU0sQ0FBQyxPQUFPLENBQUM7QUFDdkIsQ0FBQztBQUVELFNBQVMsZ0JBQWdCLENBQUMsVUFBeUI7SUFDbEQsTUFBTSxHQUFHLEdBQUcsRUFBRSxDQUFDLDRCQUE0QixDQUFDLFVBQVUsQ0FBQyxXQUFXLEVBQUUsSUFBSSxDQUFDLENBQUM7SUFFMUUsSUFBSSxDQUFDLFVBQVUsQ0FBQyxJQUFJLEVBQUU7UUFDckIsT0FBTyxTQUFTLEdBQUcsRUFBRSxDQUFDO0tBQ3RCO0lBRUQsTUFBTSxHQUFHLEdBQUcsVUFBVSxDQUFDLElBQUksQ0FBQyw2QkFBNkIsQ0FDeEQsVUFBVSxDQUFDLEtBQUssSUFBSSxDQUFDLENBQ3JCLENBQUM7SUFFRixNQUFNLElBQUksR0FBRyxJQUFJLENBQUMsUUFBUSxDQUFDLFVBQVUsQ0FBQyxJQUFJLENBQUMsUUFBUSxDQUFDLENBQUM7SUFDckQsTUFBTSxJQUFJLEdBQUcsR0FBRyxDQUFDLElBQUksR0FBRyxDQUFDLENBQUM7SUFDMUIsTUFBTSxJQUFJLEdBQUcsR0FBRyxDQUFDLFNBQVMsR0FBRyxDQUFDLENBQUM7SUFDL0IsTUFBTSxRQUFRLEdBQUcsVUFBVSxDQUFDLFFBQVEsQ0FBQztJQUNyQyxNQUFNLElBQUksR0FBRyxFQUFFLENBQUMsa0JBQWtCLENBQUMsUUFBUSxDQUFDLENBQUMsV0FBVyxFQUFFLENBQUM7SUFDM0QsTUFBTSxJQUFJLEdBQUcsVUFBVSxDQUFDLElBQUksQ0FBQztJQUU3QixPQUFPLFNBQVMsSUFBSSxJQUFJLElBQUksSUFBSSxJQUFJLE1BQU0sSUFBSSxNQUFNLElBQUksS0FBSyxHQUFHLEVBQUUsQ0FBQztBQUNwRSxDQUFDO0FBRUQsU0FBUyxxQkFBcUI7SUFDN0IsT0FBTyxFQUFFLENBQUMscUJBQXFCLENBQUM7UUFDL0Isa0JBQWtCLEVBQUUsR0FBRyxFQUFFLENBQUMsZUFBZTtRQUN6QyxnQkFBZ0IsRUFBRSxRQUFRLENBQUMsRUFBRTtZQUM1QixNQUFNLGFBQWEsR0FBRyxjQUFjLENBQUMsR0FBRyxDQUFDLFFBQVEsQ0FBQyxDQUFDO1lBQ25ELE9BQU8sYUFBYSxDQUFDLENBQUMsQ0FBQyxhQUFhLENBQUMsT0FBTyxDQUFDLFFBQVEsRUFBRSxDQUFDLENBQUMsQ0FBQyxHQUFHLENBQUM7UUFDL0QsQ0FBQztRQUNELGlCQUFpQixFQUFFLFFBQVEsQ0FBQyxFQUFFO1lBQzdCLE1BQU0sY0FBYyxHQUFHLFNBQVMsQ0FBQyxHQUFHLENBQUMsUUFBUSxDQUFDLENBQUM7WUFDL0MsSUFBSSxjQUFjLEVBQUU7Z0JBQUUsT0FBTyxjQUFjLENBQUM7YUFBRTtZQUU5QyxNQUFNLFdBQVcsR0FBRyxFQUFFLENBQUMsR0FBRyxDQUFDLFFBQVEsQ0FBQyxRQUFRLENBQUMsQ0FBQztZQUM5QyxJQUFJLFdBQVcsS0FBSyxTQUFTLEVBQUU7Z0JBQzlCLGNBQWMsQ0FBQyxNQUFNLENBQUMsUUFBUSxDQUFDLENBQUM7Z0JBQ2hDLE9BQU8sU0FBUyxDQUFDO2FBQ2pCO1lBRUQsTUFBTSxRQUFRLEdBQUcsRUFBRSxDQUFDLGNBQWMsQ0FBQyxVQUFVLENBQUMsV0FBVyxDQUFDLENBQUM7WUFHM0QsSUFBSSxRQUFRLENBQUMsTUFBTSxDQUFDLGNBQWMsQ0FBQyxLQUFLLENBQUMsQ0FBQyxFQUFFO2dCQUMzQyxTQUFTLENBQUMsR0FBRyxDQUFDLFFBQVEsRUFBRSxRQUFRLENBQUMsQ0FBQzthQUNsQztZQUVELElBQUksQ0FBQyxjQUFjLENBQUMsR0FBRyxDQUFDLFFBQVEsQ0FBQyxFQUFFO2dCQUNsQyxjQUFjLENBQUMsR0FBRyxDQUFDLFFBQVEsRUFBRTtvQkFDNUIsT0FBTyxFQUFFLENBQUM7b0JBQ1YsWUFBWSxFQUFFLElBQUksQ0FBQyxHQUFHLEVBQUU7aUJBQ3hCLENBQUMsQ0FBQzthQUNIO1lBRUQsTUFBTSxhQUFhLEdBQUcsY0FBYyxDQUFDLEdBQUcsQ0FBQyxRQUFRLENBQUMsQ0FBQztZQUNuRCxJQUFJLENBQUMsYUFBYSxFQUFFO2dCQUFFLE9BQU8sU0FBUyxDQUFDO2FBQUU7WUFFekMsTUFBTSxZQUFZLEdBQUcsZUFBZSxDQUFDLFFBQVEsQ0FBQyxDQUFDO1lBQy9DLElBQUksWUFBWSxHQUFHLGFBQWEsQ0FBQyxZQUFZLEVBQUU7Z0JBQzlDLGFBQWEsQ0FBQyxZQUFZLEdBQUcsWUFBWSxDQUFDO2dCQUMxQyxhQUFhLENBQUMsT0FBTyxJQUFJLENBQUMsQ0FBQzthQUMzQjtZQUVELE9BQU8sUUFBUSxDQUFDO1FBQ2pCLENBQUM7UUFDRCxtQkFBbUIsRUFBRSxHQUFHLEVBQUUsQ0FBQyxnQkFBZ0I7UUFDM0Msc0JBQXNCLEVBQUUsR0FBRyxFQUFFLENBQUMsZUFBZTtRQUM3QyxxQkFBcUIsRUFBRSxFQUFFLENBQUMscUJBQXFCO1FBQy9DLFVBQVUsRUFBRSxFQUFFLENBQUMsR0FBRyxDQUFDLFVBQVU7UUFDN0IsUUFBUSxFQUFFLEVBQUUsQ0FBQyxHQUFHLENBQUMsUUFBUTtRQUN6QixhQUFhLEVBQUUsRUFBRSxDQUFDLEdBQUcsQ0FBQyxhQUFhO0tBQ25DLEVBQUUsRUFBRSxDQUFDLHNCQUFzQixFQUFFLENBQUMsQ0FBQztBQUNqQyxDQUFDO0FBRUQsU0FBUyxlQUFlLENBQUMsUUFBZ0I7SUFDeEMsTUFBTSxZQUFZLEdBQUcsRUFBRSxDQUFDLEdBQUcsQ0FBQyxlQUFnQixDQUFDLFFBQVEsQ0FBQyxDQUFDO0lBQ3ZELElBQUksQ0FBQyxZQUFZLEVBQUU7UUFDbEIsTUFBTSxJQUFJLEtBQUssQ0FBQywwQ0FBMEMsUUFBUSxHQUFHLENBQUMsQ0FBQztLQUN2RTtJQUVELE9BQU8sWUFBWSxDQUFDLE9BQU8sRUFBRSxDQUFDO0FBQy9CLENBQUM7QUFFRCxTQUFTLFNBQVMsQ0FBQyxRQUFnQjtJQUNsQyxNQUFNLFlBQVksR0FBRyxlQUFlLENBQUMsUUFBUSxDQUFDLENBQUM7SUFFL0MsSUFBSSxhQUFhLEdBQUcsY0FBYyxDQUFDLEdBQUcsQ0FBQyxRQUFRLENBQUMsQ0FBQztJQUNqRCxJQUFJLENBQUMsYUFBYSxFQUFFO1FBQ25CLGFBQWEsR0FBRztZQUNmLE9BQU8sRUFBRSxDQUFDO1lBQ1YsWUFBWSxFQUFFLFlBQVk7U0FDMUIsQ0FBQztRQUVGLGVBQWUsQ0FBQyxJQUFJLENBQUMsUUFBUSxDQUFDLENBQUM7UUFDL0IsY0FBYyxDQUFDLEdBQUcsQ0FBQyxRQUFRLEVBQUUsYUFBYSxDQUFDLENBQUM7S0FDNUM7U0FDSTtRQUNKLGFBQWEsQ0FBQyxPQUFPLElBQUksQ0FBQyxDQUFDO0tBQzNCO0lBRUQsTUFBTSxPQUFPLEdBQUcsZUFBZSxDQUFDLFVBQVUsRUFBRSxDQUFDO0lBQzdDLElBQUksT0FBTyxLQUFLLFNBQVMsRUFBRTtRQUMxQixNQUFNLElBQUksS0FBSyxDQUFDLHVCQUF1QixDQUFDLENBQUM7S0FDekM7SUFFRCxNQUFNLFVBQVUsR0FBRyxPQUFPLENBQUMsYUFBYSxDQUFDLFFBQVEsQ0FBQyxDQUFDO0lBQ25ELElBQUksVUFBVSxLQUFLLFNBQVMsRUFBRTtRQUM3QixNQUFNLElBQUksS0FBSyxDQUFDLDhCQUE4QixRQUFRLEVBQUUsQ0FBQyxDQUFDO0tBQzFEO0lBRUQsTUFBTSxXQUFXLEdBQUcsRUFBRSxDQUFDLHFCQUFxQixDQUFDLE9BQU8sRUFBRSxVQUFVLENBQUMsQ0FBQztJQUNsRSxNQUFNLE1BQU0sR0FBRyxXQUFXLENBQUMsR0FBRyxDQUFDLGdCQUFnQixDQUFDLENBQUM7SUFDakQsSUFBSSxNQUFNLENBQUMsTUFBTSxHQUFHLENBQUMsRUFBRTtRQUN0QixPQUFPLEVBQUUsTUFBTSxFQUFFLE1BQU0sRUFBRSxDQUFDO0tBQzFCO0lBRUQsTUFBTSxVQUFVLEdBQUcsZUFBZSxDQUFDLGFBQWEsQ0FBQyxRQUFRLENBQUMsQ0FBQztJQUMzRCxJQUFJLFVBQVUsQ0FBQyxXQUFXLEVBQUU7UUFDM0IsTUFBTSxJQUFJLEtBQUssQ0FBQyxjQUFjLENBQUMsQ0FBQztLQUNoQztJQUVELE1BQU0sU0FBUyxHQUFHLFVBQVUsQ0FBQyxXQUFXLENBQUMsTUFBTSxDQUFDO0lBQ2hELElBQUksU0FBUyxLQUFLLENBQUMsRUFBRTtRQUNwQixNQUFNLElBQUksS0FBSyxDQUFDLHNDQUFzQyxTQUFTLEVBQUUsQ0FBQyxDQUFDO0tBQ25FO0lBRUQsYUFBYSxDQUFDLFlBQVksR0FBRyxZQUFZLENBQUM7SUFFMUMsTUFBTSxNQUFNLEdBQUcsVUFBVSxDQUFDLFdBQVcsQ0FBQyxDQUFDLENBQUMsQ0FBQztJQUN6QyxNQUFNLE9BQU8sR0FBRyxJQUFJLEdBQUcsRUFBa0IsQ0FBQztJQUMxQyxPQUFPLENBQUMsR0FBRyxDQUFDLFFBQVEsRUFBRSxNQUFNLENBQUMsSUFBSSxDQUFDLENBQUM7SUFFbkMsTUFBTSxhQUFhLEdBQUcsSUFBSSxDQUFDLEtBQUssQ0FBQyxRQUFRLENBQUMsQ0FBQyxHQUFHLENBQUM7SUFDL0MsTUFBTSxRQUFRLEdBQUcsSUFBSSxDQUFDLFFBQVEsQ0FBQyxRQUFRLENBQUMsQ0FBQztJQUN6QyxNQUFNLFNBQVMsR0FBRyxRQUFRLENBQUMsT0FBTyxDQUFDLGFBQWEsRUFBRSxFQUFFLENBQUMsQ0FBQztJQUV0RCxNQUFNLFNBQVMsR0FBRyxDQUNqQixVQUFVLENBQUMsVUFBVTtTQUNuQixNQUFNLENBQUMsRUFBRSxDQUFDLHFCQUFxQixDQUFDO1NBQ2hDLE1BQU0sQ0FBQyxDQUFDLENBQUMsRUFBRSxDQUFDLENBQUMsQ0FBQyxJQUFJLEtBQUssU0FBUyxDQUFDO1NBQ2pDLE1BQU0sQ0FBQyxVQUFVLENBQUM7U0FDbEIsR0FBRyxDQUFDLFdBQVcsQ0FBQyxFQUFFLENBQ2xCLGFBQWEsQ0FDWixXQUFXLEVBQ1gsUUFBUSxFQUNSLE9BQU8sRUFDUCxVQUFVLEVBQ1YsTUFBTSxDQUFDLENBQUM7U0FDVCxNQUFNLENBQUMsVUFBVSxDQUFDLENBQ3BCLENBQUM7SUFFRixNQUFNLFlBQVksR0FBRyxnQkFBZ0IsQ0FBQyxPQUFPLEVBQUUsVUFBVSxDQUFDLENBQUM7SUFFM0QsT0FBTztRQUNOLFFBQVEsRUFBRSxRQUFRO1FBQ2xCLElBQUksRUFBRSxTQUFTO1FBQ2YsSUFBSSxFQUFFLFFBQVE7UUFDZCxNQUFNLEVBQUUsT0FBTyxDQUFDLEdBQUcsQ0FBQyxRQUFRLENBQUMsSUFBSSxFQUFFO1FBQ25DLE1BQU0sRUFBRSxNQUFNO1FBQ2QsT0FBTyxFQUFFLFNBQVM7UUFDbEIsWUFBWSxFQUFFLFlBQVk7S0FDMUIsQ0FBQztBQUNILENBQUM7QUFFRCxTQUFTLFVBQVUsQ0FBQyxJQUFhO0lBQ2hDLE1BQU0sU0FBUyxHQUFHLElBQUksQ0FBQyxTQUFTLENBQUM7SUFDakMsT0FBTyxTQUFTLElBQUksU0FBUyxDQUFDLElBQUksQ0FBQyxHQUFHLENBQUMsRUFBRSxDQUN4QyxHQUFHLENBQUMsSUFBSSxLQUFLLEVBQUUsQ0FBQyxVQUFVLENBQUMsYUFBYSxDQUN4QyxDQUFDO0FBQ0gsQ0FBQztBQUVELFNBQVMsVUFBVSxDQUFJLEtBQTJCO0lBQ2pELE9BQU8sS0FBSyxJQUFJLFNBQVMsQ0FBQztBQUMzQixDQUFDO0FBRUQsU0FBUyxhQUFhLENBQ3JCLFdBQW1DLEVBQ25DLFFBQWdCLEVBQ2hCLE9BQW1CLEVBQ25CLFVBQXlCLEVBQ3pCLE1BQWdCO0lBRWhCLE1BQU0sV0FBVyxHQUFHLE9BQU8sQ0FBQyxjQUFjLEVBQUUsQ0FBQztJQUU3QyxNQUFNLFNBQVMsR0FBRyxXQUFXLENBQUMsMkJBQTJCLENBQUMsV0FBVyxDQUFFLENBQUM7SUFDeEUsTUFBTSxVQUFVLEdBQUcsV0FBVyxDQUFDLHdCQUF3QixDQUFDLFNBQVMsQ0FBQyxDQUFDO0lBQ25FLE1BQU0sYUFBYSxHQUFHLFdBQVcsQ0FBQyxZQUFZLENBQUMsVUFBVSxDQUFDLENBQUM7SUFDM0QsTUFBTSxXQUFXLEdBQUcsU0FBUyxDQUFDLGFBQWEsQ0FBQyxDQUFDO0lBRTdDLE1BQU0sUUFBUSxHQUFHLFdBQVcsQ0FBQyxXQUFXLEVBQUUsVUFBVSxDQUFDLENBQUM7SUFHdEQsSUFBSSxXQUFXLENBQUMsTUFBTSxHQUFHLENBQUMsRUFBRTtRQUMzQixNQUFNLElBQUksR0FBRyxJQUFJLENBQUMsUUFBUSxDQUFDLFFBQVEsQ0FBQyxDQUFDO1FBQ3JDLE1BQU0sSUFBSSxHQUFHLFFBQVEsQ0FBQyxJQUFJLENBQUM7UUFDM0IsTUFBTSxJQUFJLEdBQUcsUUFBUSxDQUFDLFNBQVMsQ0FBQztRQUVoQyxNQUFNLENBQUMsSUFBSSxDQUNWLFNBQVMsSUFBSSxJQUFJLElBQUksSUFBSSxJQUFJLEtBQUs7WUFDbEMsaUNBQWlDLGFBQWEsR0FBRyxDQUNqRCxDQUFDO1FBRUYsT0FBTyxJQUFJLENBQUM7S0FDWjtJQUVELE1BQU0sVUFBVSxHQUFHLFdBQVcsQ0FBQyxVQUFVLENBQUMsR0FBRyxDQUFDLEtBQUssQ0FBQyxFQUFFO1FBQ3JELE9BQU8sY0FBYyxDQUFDLEtBQUssRUFBRSxXQUFXLENBQUMsQ0FBQztJQUMzQyxDQUFDLENBQUMsQ0FBQztJQUVILE9BQU87UUFDTixJQUFJLEVBQUUsV0FBVyxDQUFDLElBQUssQ0FBQyxPQUFPLEVBQUU7UUFDakMsVUFBVSxFQUFFLFVBQVU7UUFDdEIsV0FBVyxFQUFFLFdBQVc7UUFDeEIsSUFBSSxFQUFFLFFBQVEsQ0FBQyxJQUFJO1FBQ25CLFNBQVMsRUFBRSxRQUFRLENBQUMsU0FBUztLQUM3QixDQUFDO0FBQ0gsQ0FBQztBQUVELFNBQVMsY0FBYyxDQUN0QixLQUE4QixFQUM5QixXQUEyQjtJQUUzQixNQUFNLElBQUksR0FBRyxXQUFXLENBQUMsaUJBQWlCLENBQUMsS0FBSyxDQUFDLElBQUssQ0FBQyxDQUFDO0lBQ3hELE1BQU0sS0FBSyxHQUFHLFNBQVMsQ0FBQyxXQUFXLENBQUMsWUFBWSxDQUFDLElBQUksQ0FBQyxDQUFDLENBQUM7SUFDeEQsTUFBTSxRQUFRLEdBQUcsQ0FDaEIsS0FBSyxDQUFDLFdBQVcsS0FBSyxTQUFTO1FBQzlCLENBQUMsQ0FBQyxXQUFXLENBQUMsbUJBQW1CLENBQUMsS0FBSyxDQUFDO1FBQ3hDLENBQUMsQ0FBQyxLQUFLLENBQ1IsQ0FBQztJQUVGLE9BQU87UUFDTixJQUFJLEVBQUUsS0FBSyxDQUFDLElBQUksQ0FBQyxPQUFPLEVBQUU7UUFDMUIsS0FBSyxFQUFFLEtBQUs7UUFDWixRQUFRLEVBQUUsUUFBUTtLQUNsQixDQUFDO0FBQ0gsQ0FBQztBQUVELFNBQVMsU0FBUyxDQUFDLE9BQWU7SUFDakMsT0FBTyxPQUFPLENBQUMsS0FBSyxDQUFDLEtBQUssQ0FBQyxDQUFDLEdBQUcsQ0FBQyxHQUFHLENBQUMsRUFBRTtRQUNyQyxNQUFNLElBQUksR0FBRyxHQUFHLENBQUMsT0FBTyxDQUFDLE9BQU8sRUFBRSxFQUFFLENBQUMsQ0FBQztRQUN0QyxNQUFNLFVBQVUsR0FBRyxDQUFDLEdBQUcsQ0FBQyxLQUFLLENBQUMsT0FBTyxDQUFDLElBQUksRUFBRSxDQUFDLENBQUMsTUFBTSxDQUFDO1FBQ3JELE9BQU87WUFDTixJQUFJLEVBQUUsSUFBSTtZQUNWLFVBQVUsRUFBRSxVQUFVO1NBQ3RCLENBQUM7SUFDSCxDQUFDLENBQUMsQ0FBQztBQUNKLENBQUM7QUFFRCxTQUFTLFdBQVcsQ0FBQyxJQUFhLEVBQUUsVUFBeUI7SUFDNUQsTUFBTSxHQUFHLEdBQUcsVUFBVSxDQUFDLDZCQUE2QixDQUNuRCxJQUFJLENBQUMsUUFBUSxDQUFDLFVBQVUsRUFBRSxJQUFJLENBQUMsQ0FDL0IsQ0FBQztJQUVGLEdBQUcsQ0FBQyxJQUFJLElBQUksQ0FBQyxDQUFDO0lBQ2QsR0FBRyxDQUFDLFNBQVMsSUFBSSxDQUFDLENBQUM7SUFFbkIsT0FBTyxHQUFHLENBQUM7QUFDWixDQUFDO0FBRUQsU0FBUyxnQkFBZ0IsQ0FDeEIsT0FBbUIsRUFDbkIsVUFBeUI7SUFFekIsTUFBTSxXQUFXLEdBQUcsT0FBTyxDQUFDLGNBQWMsRUFBRSxDQUFDO0lBRTdDLE1BQU0sU0FBUyxHQUFHLENBQUMsSUFBYSxFQUFXLEVBQUU7UUFDNUMsTUFBTSxRQUFRLEdBQUcsV0FBVyxDQUFDLFlBQVksQ0FBQyxJQUFJLENBQUMsQ0FBQztRQUNoRCxJQUFJLFFBQVEsS0FBSyxTQUFTLEVBQUU7WUFBRSxPQUFPLElBQUksQ0FBQztTQUFFO1FBRTVDLE1BQU0sU0FBUyxHQUFHLElBQUksQ0FBQyxZQUFZLEVBQUUsSUFBSSxFQUFFLENBQUM7UUFDNUMsT0FBTyxDQUFDLENBQUMsU0FBUyxDQUFDLElBQUksQ0FBQyxTQUFTLENBQUMsQ0FBQztJQUNwQyxDQUFDLENBQUM7SUFFRixNQUFNLFlBQVksR0FBRyxJQUFJLEdBQUcsRUFBVSxDQUFDO0lBRXZDLE1BQU0sWUFBWSxHQUFHLENBQUMsSUFBYSxFQUFFLEVBQUU7UUFDdEMsRUFBRSxDQUFDLFlBQVksQ0FBQyxJQUFJLEVBQUUsWUFBWSxDQUFDLENBQUM7UUFFcEMsSUFBSSxJQUFJLENBQUM7UUFDVCxJQUFJO1lBQUUsSUFBSSxHQUFHLFdBQVcsQ0FBQyxpQkFBaUIsQ0FBQyxJQUFJLENBQUMsQ0FBQztTQUFFO1FBQ25ELFdBQU07WUFBRSxPQUFPO1NBQUU7UUFFakIsSUFBSSxDQUFDLElBQUksRUFBRTtZQUFFLE9BQU87U0FBRTtRQUV0QixNQUFNLFFBQVEsR0FBRyxDQUFDLEVBQUUsQ0FBQyxTQUFTLENBQUMsSUFBSSxDQUFDLEtBQUssQ0FBQyxJQUFJLEVBQUUsQ0FBQyxDQUFDO1FBQ2xELE1BQU0sU0FBUyxHQUFHLENBQUMsUUFBUSxDQUFDLE1BQU0sQ0FBQyxTQUFTLENBQUMsS0FBSyxDQUFDLENBQUMsQ0FBQyxDQUFDO1FBQ3RELE1BQU0sVUFBVSxHQUFHLENBQUMsSUFBSSxDQUFDLGlCQUFpQixFQUFFLENBQUMsTUFBTSxDQUFDO1FBQ3BELE1BQU0sV0FBVyxHQUFHLENBQUMsU0FBUyxJQUFJLFVBQVUsQ0FBQztRQUU3QyxJQUFJLENBQUMsV0FBVyxFQUFFO1lBQUUsT0FBTztTQUFFO1FBRTdCLE1BQU0sbUJBQW1CLEdBQUcsSUFBSSxDQUFDLHNCQUFzQixFQUFFLENBQUM7UUFDMUQsSUFBSSxtQkFBbUIsQ0FBQyxNQUFNLEVBQUU7WUFDL0IsSUFBSSxHQUFHLG1CQUFtQixDQUFDLENBQUMsQ0FBQyxDQUFDLGFBQWEsRUFBRSxDQUFDO1NBQzlDO1FBRUQsSUFBSSxTQUFTLENBQUMsSUFBSSxDQUFDLEVBQUU7WUFDcEIsTUFBTSxRQUFRLEdBQUcsV0FBVyxDQUFDLFlBQVksQ0FBQyxJQUFJLENBQUMsQ0FBQztZQUNoRCxZQUFZLENBQUMsR0FBRyxDQUFDLFFBQVEsQ0FBQyxDQUFDO1NBQzNCO0lBQ0YsQ0FBQyxDQUFDO0lBRUYsWUFBWSxDQUFDLFVBQVUsQ0FBQyxDQUFDO0lBRXpCLE9BQU8sS0FBSyxDQUFDLElBQUksQ0FBQyxZQUFZLENBQUMsQ0FBQztBQUNqQyxDQUFDO0FBRUQsU0FBUyxhQUFhLENBQUMsUUFBMkI7SUFDakQsSUFBSSxPQUFPLFFBQVEsS0FBSyxRQUFRLEVBQUU7UUFDakMsUUFBUSxHQUFHLElBQUksQ0FBQyxTQUFTLENBQUMsUUFBUSxDQUFDLENBQUM7S0FDcEM7SUFFRCxPQUFPLENBQUMsTUFBTSxDQUFDLEtBQUssQ0FBQyxpQkFBaUIsR0FBRyxRQUFRLEdBQUcsSUFBSSxDQUFDLENBQUM7SUFFMUQsT0FBTyxRQUFRLENBQUM7QUFDakIsQ0FBQztBQUVELFNBQVMsY0FBYyxDQUFDLFVBQWtCO0lBQ3pDLE1BQU0sT0FBTyxHQUFHLElBQUksQ0FBQyxLQUFLLENBQUMsVUFBVSxDQUFZLENBQUM7SUFJbEQsTUFBTSxjQUFjLEdBQUcsYUFBYSxDQUFDLEdBQUcsQ0FBQyxPQUFPLENBQUMsSUFBSSxDQUFDLENBQUM7SUFDdkQsSUFBSSxjQUFjLEVBQUU7UUFDbkIsYUFBYSxDQUFDLGNBQWMsQ0FBQyxDQUFDO1FBQzlCLE9BQU87S0FDUDtJQUVELElBQUksUUFBbUIsQ0FBQztJQUN4QixJQUFJO1FBQ0gsUUFBUSxHQUFHLFNBQVMsQ0FBQyxPQUFPLENBQUMsSUFBSSxDQUFDLENBQUM7S0FDbkM7SUFBQyxPQUFNLEdBQUcsRUFBRTtRQUNaLE9BQU8sQ0FBQyxNQUFNLENBQUMsS0FBSyxDQUFDLEdBQUcsQ0FBQyxPQUFPLEdBQUcsSUFBSSxDQUFDLENBQUM7UUFDekMsT0FBTyxDQUFDLElBQUksQ0FBQyxDQUFDLENBQUMsQ0FBQyxDQUFDO1FBQ2pCLE9BQU87S0FDUDtJQUVELE1BQU0sV0FBVyxHQUFHLGFBQWEsQ0FBQyxRQUFRLENBQUMsQ0FBQztJQUM1QyxhQUFhLENBQUMsR0FBRyxDQUFDLE9BQU8sQ0FBQyxJQUFJLEVBQUUsV0FBVyxDQUFDLENBQUM7QUFTOUMsQ0FBQztBQUVELE9BQU8sQ0FBQyxLQUFLLENBQUMsV0FBVyxDQUFDLE1BQU0sQ0FBQyxDQUFDO0FBQ2xDLElBQUksTUFBTSxHQUFHLFFBQVEsQ0FBQyxlQUFlLENBQUM7SUFDckMsS0FBSyxFQUFFLE9BQU8sQ0FBQyxLQUFLO0lBQ3BCLE1BQU0sRUFBRSxPQUFPLENBQUMsTUFBTTtJQUN0QixRQUFRLEVBQUUsS0FBSztDQUNmLENBQUMsQ0FBQTtBQUVGLE1BQU0sQ0FBQyxFQUFFLENBQUMsTUFBTSxFQUFFLFVBQVUsSUFBWTtJQUN2QyxJQUFJLElBQUksR0FBRyxJQUFJLENBQUMsSUFBSSxFQUFFLENBQUM7SUFFdkIsT0FBTyxDQUFDLEdBQUcsQ0FBQyxHQUFHLEVBQUUsSUFBSSxFQUFFLEdBQUcsQ0FBQyxDQUFDO0lBRTVCLElBQUksSUFBSSxJQUFJLE1BQU0sRUFBRTtRQUNuQixPQUFPLENBQUMsR0FBRyxDQUFDLFdBQVcsQ0FBQyxDQUFDO1FBQ3pCLE9BQU8sQ0FBQyxJQUFJLENBQUMsQ0FBQyxDQUFDLENBQUM7UUFDaEIsT0FBTztLQUNQO0lBRUQsSUFBSSxJQUFJLENBQUMsTUFBTSxHQUFHLENBQUMsRUFBRTtRQUNwQixjQUFjLENBQUMsSUFBSSxDQUFDLENBQUM7S0FDckI7QUFDRixDQUFDLENBQUMsQ0FBQTtBQUVGLE9BQU8sQ0FBQyxHQUFHLENBQUMsYUFBYSxDQUFDLENBQUEifQ==