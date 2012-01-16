# JavaScriptCore - Qt4 build info

include(../common.pri)

VPATH += $$PWD

# Use a config-specific target to prevent parallel builds file clashes on Mac
mac: CONFIG(debug, debug|release): JAVASCRIPTCORE_TARGET = jscored
else: JAVASCRIPTCORE_TARGET = jscore

# Output in JavaScriptCore/<config>
CONFIG(debug, debug|release) : JAVASCRIPTCORE_DESTDIR = debug
else: JAVASCRIPTCORE_DESTDIR = release

CONFIG(standalone_package) {
    isEmpty(JSC_GENERATED_SOURCES_DIR):JSC_GENERATED_SOURCES_DIR = $$PWD/generated
} else {
    isEmpty(JSC_GENERATED_SOURCES_DIR):JSC_GENERATED_SOURCES_DIR = generated
}

CONFIG(standalone_package): DEFINES *= NDEBUG

symbian: {
    # Need to guarantee this comes before system includes of /epoc32/include
    MMP_RULES += "USERINCLUDE ../JavaScriptCore/profiler"
    LIBS += -lhal
    # For hal.h
    INCLUDEPATH *= $$MW_LAYER_SYSTEMINCLUDE
}

INCLUDEPATH = \
    $$PWD \
    $$PWD/.. \
    $$PWD/assembler \
    $$PWD/bytecode \
    $$PWD/bytecompiler \
    $$PWD/debugger \
    $$PWD/interpreter \
    $$PWD/jit \
    $$PWD/parser \
    $$PWD/pcre \
    $$PWD/profiler \
    $$PWD/runtime \
    $$PWD/wtf \
    $$PWD/wtf/symbian \
    $$PWD/wtf/text \
    $$PWD/wtf/unicode \
    $$PWD/yarr \
    $$PWD/API \
    $$PWD/ForwardingHeaders \
    $$JSC_GENERATED_SOURCES_DIR \
    $$INCLUDEPATH

win32-*: DEFINES += _HAS_TR1=0

DEFINES += BUILDING_OLYMPIA__ BUILDING_JavaScriptCore BUILDING_WTF JS_BLOCK_TOTAL_SIZE=2097152

contains(JAVASCRIPTCORE_JIT,yes): DEFINES+=ENABLE_JIT=1
contains(JAVASCRIPTCORE_JIT,no): DEFINES+=ENABLE_JIT=0

wince* {
    INCLUDEPATH += $$QT_SOURCE_TREE/src/3rdparty/ce-compat
}
wince*|win32-msvc-fledge {
    INCLUDEPATH += $$PWD/../JavaScriptCore/os-win32
}

olympia-armcc-* {
    DEFINES+=ENABLE_JIT=1
}

olympia-armcc-xscale|olympia-armcc-pj4|olympia-armcc-pj1|olympia-armcc-msm8k {
    DEFINES+=ENABLE_ASSEMBLER_WX_EXCLUSIVE=1
    DEFINES+=INST_CMN=1 INST_STRD=1 INST_LDRD=1 INST_VMOVD=1 OPT_DEAD_CODE=1 OPT_SCHED=1 OPT_FASTACCESS=1
}

defineTest(addJavaScriptCoreLib) {
    # Argument is the relative path to JavaScriptCore.pro's qmake output
    pathToJavaScriptCoreOutput = $$ARGS/$$JAVASCRIPTCORE_DESTDIR

    win32-msvc-fledge {
        # Do nothing
    } else:win32-msvc*|wince* {
        LIBS += -L$$pathToJavaScriptCoreOutput
        LIBS += -l$$JAVASCRIPTCORE_TARGET
        POST_TARGETDEPS += $${pathToJavaScriptCoreOutput}$${QMAKE_DIR_SEP}$${JAVASCRIPTCORE_TARGET}.lib
    } else:symbian {
        LIBS += -l$${JAVASCRIPTCORE_TARGET}.lib
        # The default symbian build system does not use library paths at all. However when building with
        # qmake's symbian makespec that uses Makefiles
        QMAKE_LIBDIR += $$pathToJavaScriptCoreOutput
        POST_TARGETDEPS += $${pathToJavaScriptCoreOutput}$${QMAKE_DIR_SEP}$${JAVASCRIPTCORE_TARGET}.lib
    } else:olympia-armcc-* {
        # Do nothing
    } else {
        # Make sure jscore will be early in the list of libraries to workaround a bug in MinGW
        # that can't resolve symbols from QtCore if libjscore comes after.
        QMAKE_LIBDIR = $$pathToJavaScriptCoreOutput $$QMAKE_LIBDIR
        LIBS += -l$$JAVASCRIPTCORE_TARGET
        POST_TARGETDEPS += $${pathToJavaScriptCoreOutput}$${QMAKE_DIR_SEP}lib$${JAVASCRIPTCORE_TARGET}.a
    }

    win32-* {
        LIBS += -lwinmm
    }

    # The following line is to prevent qmake from adding jscore to libQtWebKit's prl dependencies.
    # The compromise we have to accept by disabling explicitlib is to drop support to link QtWebKit and QtScript
    # statically in applications (which isn't used often because, among other things, of licensing obstacles).
    CONFIG -= explicitlib

    export(QMAKE_LIBDIR)
    export(LIBS)
    export(POST_TARGETDEPS)
    export(CONFIG)

    return(true)
}

OLYMPIA_PTHREAD = $$(FEATURE_OLYMPIA_PTHREAD)
isEmpty(OLYMPIA_PTHREAD) {
    DEFINES += ENABLE_JSC_MULTIPLE_THREADS=0
    DEFINES += WTF_USE_PTHREADS=1
} else {
    DEFINES += ENABLE_SINGLE_THREADED=1
    DEFINES += ENABLE_JSC_MULTIPLE_THREADS=0
}

OLYMPIA_LOG = $$(FEATURE_OLYMPIA_LOG)
!isEmpty(OLYMPIA_LOG) {
    DEFINES += LOG_DISABLED=0
    DEFINES += ERROR_DISABLED=0
} else {
    DEFINES += LOG_DISABLED=1
    DEFINES += ERROR_DISABLED=1
}

BUILD_REQUEST_TYPE = $$(BUILD_REQUEST_TYPE)
contains(BUILD_REQUEST_TYPE, SCM) {
    DEFINES += PUBLIC_BUILD=1
} else {
    DEFINES += PUBLIC_BUILD=0
}

DEFINES += SYSTEMINCLUDE=$$(SYSTEMINCDIR)