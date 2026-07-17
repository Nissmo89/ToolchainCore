#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "ccc/engine.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qDebug() << "--- STARTING TOOLCHAINCORE TESTS ---";

    ccc::Engine engine;

    // Create a temporary workspace directory inside build/test_workspace
    QDir().mkpath("test_workspace");
    QDir testDir("test_workspace");
    QString testDirPath = testDir.absolutePath();

    // 1. Write a C++ test file
    QString cppSourcePath = testDir.filePath("hello.cpp");
    QFile cppFile(cppSourcePath);
    if (!cppFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCritical() << "FAIL: Could not write hello.cpp";
        return 1;
    }
    cppFile.write(
        "#include <iostream>\n"
        "int main() {\n"
        "    std::cout << \"Hello from Compiled executable!\" << std::endl;\n"
        "    return 0;\n"
        "}\n"
    );
    cppFile.close();

    // 2. Test compiling C++ source to Object File (Part 2)
    qDebug() << "Test 1: Compiling hello.cpp to hello.o ...";
    ccc::CompileConfig objConfig;
    objConfig.language = ccc::Language::Cpp;
    objConfig.outputType = ccc::OutputType::ObjectFile;
    objConfig.optimizationLevel = ccc::OptimizationLevel::Speed;
    objConfig.debugInfo = true;
    objConfig.compilationDatabasePath = testDirPath;

    QString objPath = testDir.filePath("hello.o");
    ccc::CompilationResult compileResult = engine.compile(cppSourcePath, objPath, objConfig);
    if (!compileResult.success) {
        qCritical() << "FAIL: Compilation to object file failed." << compileResult.errorMessage;
        qCritical() << "stderr:" << compileResult.stdErr;
        return 1;
    }
    qDebug() << "PASS: Successfully compiled to object file:" << compileResult.outputPath;
    if (!QFile::exists(objPath)) {
        qCritical() << "FAIL: Output object file does not exist at expected path.";
        return 1;
    }

    // Verify compilation database generation
    qDebug() << "Test 1b: Checking compile_commands.json ...";
    QString dbFilePath = testDir.filePath("compile_commands.json");
    if (!QFile::exists(dbFilePath)) {
        qCritical() << "FAIL: compile_commands.json was not created.";
        return 1;
    }
    QFile dbFile(dbFilePath);
    if (!dbFile.open(QIODevice::ReadOnly)) {
        qCritical() << "FAIL: Could not open compile_commands.json.";
        return 1;
    }
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(dbFile.readAll(), &parseErr);
    dbFile.close();
    if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
        qCritical() << "FAIL: compile_commands.json is invalid JSON or not an array." << parseErr.errorString();
        return 1;
    }
    QJsonArray array = doc.array();
    if (array.isEmpty()) {
        qCritical() << "FAIL: compile_commands.json is empty.";
        return 1;
    }
    QJsonObject entry = array.first().toObject();
    QString dbFileEntry = entry.value("file").toString();
    QString expectedSource = QFileInfo(cppSourcePath).absoluteFilePath();
    if (QDir::cleanPath(dbFileEntry) != QDir::cleanPath(expectedSource)) {
        qCritical() << "FAIL: compile_commands.json entry file mismatch. Expected:" << expectedSource << "Got:" << dbFileEntry;
        return 1;
    }
    qDebug() << "PASS: compile_commands.json verified successfully.";

    // 3. Test Linking the Object File into an Executable (Part 3)
    qDebug() << "Test 2: Linking hello.o to executable ...";
    ccc::CompileConfig linkConfig;
    linkConfig.language = ccc::Language::Cpp;
    linkConfig.outputType = ccc::OutputType::Executable;
    linkConfig.debugInfo = true;

    QString execPath = testDir.filePath("hello_exec");
#ifdef Q_OS_WIN
    execPath += ".exe";
#endif

    ccc::CompilationResult linkResult = engine.link({objPath}, execPath, linkConfig);
    if (!linkResult.success) {
        qCritical() << "FAIL: Linking failed." << linkResult.errorMessage;
        qCritical() << "stderr:" << linkResult.stdErr;
        return 1;
    }
    qDebug() << "PASS: Successfully linked to executable:" << linkResult.outputPath;

    // 4. Run the executable and verify output
    qDebug() << "Test 3: Running executable and verifying output ...";
    ccc::ExecutionResult runResult = engine.runExecutable(execPath);
    if (!runResult.success) {
        qCritical() << "FAIL: Running executable failed." << runResult.errorMessage;
        return 1;
    }
    qDebug() << "Executable stdout:" << runResult.stdOut.trimmed();
    if (!runResult.stdOut.contains("Hello from Compiled executable!")) {
        qCritical() << "FAIL: Executable stdout mismatch.";
        return 1;
    }
    qDebug() << "PASS: Executable outputs expected message.";

    // 5. Test JIT memory compilation & function symbol resolution if available
    qDebug() << "Test 4: In-Memory TCC JIT symbol execution ...";
    ccc::CompileConfig jitConfig;
    jitConfig.backendKind = ccc::BackendKind::InternalTcc;
    jitConfig.outputType = ccc::OutputType::Memory;

    QString cSourcePath = testDir.filePath("add.c");
    QFile cFile(cSourcePath);
    if (cFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        cFile.write(
            "int add(int a, int b) {\n"
            "    return a + b;\n"
            "}\n"
        );
        cFile.close();

        ccc::CompilationResult jitResult = engine.compile(cSourcePath, "", jitConfig);
        if (jitResult.success) {
            qDebug() << "PASS: JIT Compilation succeeded.";
            void* addFuncPtr = ccc::Engine::getSymbolAddress(jitResult, "add");
            if (addFuncPtr) {
                typedef int (*AddFn)(int, int);
                AddFn addFunc = reinterpret_cast<AddFn>(addFuncPtr);
                int val = addFunc(40, 2);
                qDebug() << "JIT function call returned:" << val;
                if (val == 42) {
                    qDebug() << "PASS: JIT execution succeeded!";
                } else {
                    qCritical() << "FAIL: JIT execution returned incorrect value:" << val;
                    return 1;
                }
            } else {
                qCritical() << "FAIL: Could not resolve symbol 'add' from JIT context.";
                return 1;
            }
        } else {
            qDebug() << "INFO: JIT compilation skipped/unavailable (libtcc not loaded). Message:" << jitResult.errorMessage;
        }
    }

    qDebug() << "--- ALL TESTS PASSED SUCCESSFULLY! ---";
    return 0;
}
