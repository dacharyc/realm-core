#!groovy

@Library('realm-ci') _

cocoaStashes = []
androidStashes = []
publishingStashes = []
dependencies = null

tokens = "${env.JOB_NAME}".tokenize('/')
org = tokens[tokens.size()-3]
repo = tokens[tokens.size()-2]
branch = tokens[tokens.size()-1]

ctest_cmd = "ctest -VV"
warningFilters = [
    excludeFile('/external/*'), // submodules and external libraries
    excludeFile('/libuv-src/*'), // libuv, where it was downloaded and built inside cmake
    excludeFile('/src/realm/parser/generated/*'), // the auto generated parser code we didn't write
]

jobWrapper {
    stage('gather-info') {
        isPullRequest = !!env.CHANGE_TARGET
        targetBranch = isPullRequest ? env.CHANGE_TARGET : "none"
        rlmNode('docker') {
            getSourceArchive()
            stash includes: '**', name: 'core-source', useDefaultExcludes: false

            dependencies = readProperties file: 'dependencies.list'
            echo "Version in dependencies.list: ${dependencies.VERSION}"
            gitTag = readGitTag()
            gitSha = sh(returnStdout: true, script: 'git rev-parse HEAD').trim().take(8)
            gitDescribeVersion = sh(returnStdout: true, script: 'git describe --tags').trim()

            echo "Git tag: ${gitTag ?: 'none'}"
            if (!gitTag) {
                echo "No tag given for this build"
                setBuildName(gitSha)
            } else {
                if (gitTag != "v${dependencies.VERSION}") {
                    error "Git tag '${gitTag}' does not match v${dependencies.VERSION}"
                } else {
                    echo "Building release: '${gitTag}'"
                    setBuildName("Tag ${gitTag}")
                }
            }
            targetSHA1 = 'NONE'
            if (isPullRequest) {
                targetSHA1 = sh(returnStdout: true, script: "git fetch origin && git merge-base origin/${targetBranch} HEAD").trim()
            }

            isCoreCronJob = isCronJob()
            requireNightlyBuild = false
            if(isCoreCronJob) {
                requireNightlyBuild = isNightlyBuildNeeded()
            }
        }

        currentBranch = env.BRANCH_NAME
        println "Building branch: ${currentBranch}"
        println "Target branch: ${targetBranch}"
        releaseTesting = targetBranch.contains('release')
        isMaster = currentBranch.contains('master')
        longRunningTests = isMaster || currentBranch.contains('next-major')
        isPublishingRun = false
        if (gitTag) {
            isPublishingRun = currentBranch.contains('release')
        }
        else if(isCoreCronJob && requireNightlyBuild) {
            isPublishingRun = true
            longRunningTests = true
            def localDate = java.time.LocalDateTime.now().format(java.time.format.DateTimeFormatter.BASIC_ISO_DATE)
            gitDescribeVersion = "v${dependencies.VERSION}-nightly-${localDate}"
        }

        echo "Pull request: ${isPullRequest ? 'yes' : 'no'}"
        echo "Release Run: ${releaseTesting ? 'yes' : 'no'}"
        echo "Publishing Run: ${isPublishingRun ? 'yes' : 'no'}"
        echo "Is Realm cron job: ${isCoreCronJob ? 'yes' : 'no'}"
        echo "Is nightly build: ${requireNightlyBuild ? 'yes' : 'no'}"
        echo "Long running test: ${longRunningTests ? 'yes' : 'no'}"

        if(isCoreCronJob && !requireNightlyBuild) {
            currentBuild.result = 'ABORTED'
            error 'Nightly build is not needed because there are no new commits to build'
        }

        if (isMaster) {
            // If we're on master, instruct the docker image builds to push to the
            // cache registry
            env.DOCKER_PUSH = "1"
        }
    }

    if (isPullRequest) {
        stage('FormatCheck') {
            rlmNode('docker') {
                getArchive()

                buildDockerEnv('testing.Dockerfile').inside {
                    echo "Checking code formatting"
                    // Passing extentions because clang-format version we currently use, incorrectly tries
                    // to format json files even when configured not to. And it makes them invalid JSON.
                    // That flag can be removed once we upgrade the docker image to use a newer clang-format.
                    def clang_format = "git clang-format --extensions h,hpp,c,cpp,m,mm --diff ${targetSHA1}"
                    modifications = sh(returnStdout: true, script: clang_format).trim()
                    try {
                        if (!modifications.equals('no modified files to format')) {
                            if (!modifications.equals('clang-format did not modify any files')) {
                                echo "Commit violates formatting rules"
                                sh "${clang_format} > format_error.txt"
                                archiveArtifacts('format_error.txt')
                                sh 'exit 1'
                            }
                        }
                        currentBuild.result = 'SUCCESS'
                    } catch (Exception err) {
                        currentBuild.result = 'FAILURE'
                        throw err
                    }
                }
            }
        }
    }

    stage('Checking') {
        def buildOptions = [
            buildType : 'Debug',
            maxBpNodeSize: 1000,
            enableEncryption: true,
            useEncryption: false,
            enableSync: false,
            runTests: true,
        ]

        parallelExecutors = [
            buildLinuxRelease       : doBuildLinux('Release'),
            checkLinuxDebug         : doCheckInDocker(buildOptions),
            checkLinuxDebugEncrypt  : doCheckInDocker(buildOptions + [useEncryption : true]),
            checkLinuxRelease_4     : doCheckInDocker(buildOptions + [maxBpNodeSize: 4, buildType : 'Release']),
            checkLinuxDebug_Sync    : doCheckInDocker(buildOptions + [enableSync: true, dumpChangesetTransform: true]),
            checkLinuxDebugNoEncryp : doCheckInDocker(buildOptions + [enableEncryption: false]),
            checkMacOsRelease_Sync  : doBuildMacOs(buildOptions + [buildType: 'Release', enableSync: true]),
            checkWindows_x86_Release: doBuildWindows('Release', false, 'Win32', true),
            checkWindows_x64_Debug  : doBuildWindows('Debug', false, 'x64', true),
            buildUWP_x86_Release    : doBuildWindows('Release', true, 'Win32', false),
            buildWindows_ARM64_Debug: doBuildWindows('Debug', false, 'ARM64', false),
            buildUWP_ARM64_Debug    : doBuildWindows('Debug', true, 'ARM64', false),
            checkiOSSimulator_Debug : doBuildApplePlatform('iphonesimulator', 'Debug', true),
            buildAppleTV_Debug      : doBuildApplePlatform('appletvos', 'Debug', false),
            buildAndroidArm64Debug  : doAndroidBuildInDocker('arm64-v8a', 'Debug'),
            buildAndroidTestsArmeabi: doAndroidBuildInDocker('armeabi-v7a', 'Debug', TestAction.Build),
            threadSanitizer         : doCheckSanity(buildOptions + [enableSync: true, sanitizeMode: 'thread']),
            addressSanitizer        : doCheckSanity(buildOptions + [enableSync: true, sanitizeMode: 'address']),
        ]
        if (releaseTesting) {
            extendedChecks = [
                checkMacOsDebug               : doBuildMacOs(buildOptions + [buildType: "Release"]),
                checkAndroidarmeabiDebug      : doAndroidBuildInDocker('armeabi-v7a', 'Debug', TestAction.Run),
                // FIXME: https://github.com/realm/realm-core/issues/4159
                //checkAndroidx86Release        : doAndroidBuildInDocker('x86', 'Release', TestAction.Run),
                // FIXME: https://github.com/realm/realm-core/issues/4162
                //coverage                      : doBuildCoverage(),
            ]
            parallelExecutors.putAll(extendedChecks)
        }
        parallel parallelExecutors
    }

    if (isPublishingRun) {

        stage('BuildPackages') {
            def buildOptions = [
                enableSync: "ON",
                runTests: false,
            ]

            parallelExecutors = [
                buildMacOsRelease   : doBuildMacOs(buildOptions + [buildType : "Release"]),
                buildCatalystRelease: doBuildApplePlatform('maccatalyst', 'Release'),

                buildLinuxASAN      : doBuildLinuxClang("RelASAN"),
                buildLinuxTSAN      : doBuildLinuxClang("RelTSAN")
            ]

            androidAbis = ['armeabi-v7a', 'x86', 'x86_64', 'arm64-v8a']
            androidBuildTypes = ['Debug', 'Release']

            for (abi in androidAbis) {
                for (buildType in androidBuildTypes) {
                    parallelExecutors["android-${abi}-${buildType}"] = doAndroidBuildInDocker(abi, buildType)
                }
            }

            appleSdks = ['iphoneos', 'iphonesimulator',
                         'appletvos', 'appletvsimulator',
                         'watchos', 'watchsimulator']

            for (sdk in appleSdks) {
                parallelExecutors[sdk] = doBuildApplePlatform(sdk, 'Release')
            }

            linuxBuildTypes = ['Debug', 'Release', 'RelAssert']
            for (buildType in linuxBuildTypes) {
                parallelExecutors["buildLinux${buildType}"] = doBuildLinux(buildType)
            }

            windowsBuildTypes = ['Debug', 'Release']
            windowsPlatforms = ['Win32', 'x64', 'ARM64']

            for (buildType in windowsBuildTypes) {
                for (platform in windowsPlatforms) {
                    parallelExecutors["buildWindows-${platform}-${buildType}"] = doBuildWindows(buildType, false, platform, false)
                    parallelExecutors["buildWindowsUniversal-${platform}-${buildType}"] = doBuildWindows(buildType, true, platform, false)
                }
                parallelExecutors["buildWindowsUniversal-ARM-${buildType}"] = doBuildWindows(buildType, true, 'ARM', false)
            }

            parallel parallelExecutors
        }
        stage('Aggregate Cocoa xcframeworks') {
            rlmNode('osx') {
                getArchive()
                for (cocoaStash in cocoaStashes) {
                    unstash name: cocoaStash
                }
                sh "tools/build-cocoa.sh -x -v \"${gitDescribeVersion}\""
                archiveArtifacts('realm-*.tar.xz')
                stash includes: 'realm-*.tar.xz', name: "cocoa"
                publishingStashes << "cocoa"
            }
        }
        stage('Publish to S3') {
            rlmNode('docker') {
                deleteDir()
                dir('temp') {
                    withAWS(credentials: 'tightdb-s3-ci', region: 'us-east-1') {
                        for (publishingStash in publishingStashes) {
                            dir(publishingStash) {
                                unstash name: publishingStash
                                def path = publishingStash.replaceAll('___', '/')
                                def files = findFiles(glob: '**')
                                for (file in files) {
                                    s3Upload file: file.path, path: "downloads/core/${gitDescribeVersion}/${path}/${file.name}", bucket: 'static.realm.io'
                                    if (!requireNightlyBuild) { // don't publish nightly builds in the non-versioned folder path
                                        s3Upload file: file.path, path: "downloads/core/${file.name}", bucket: 'static.realm.io'
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

def doCheckInDocker(Map options = [:]) {
    def cmakeOptions = [
        CMAKE_BUILD_TYPE: options.buildType,
        REALM_MAX_BPNODE_SIZE: options.maxBpNodeSize,
        REALM_ENABLE_ENCRYPTION: options.enableEncryption ? 'ON' : 'OFF',
        REALM_ENABLE_SYNC: options.enableSync ? 'ON' : 'OFF',
    ]
    if (options.enableSync) {
        cmakeOptions << [
            REALM_ENABLE_AUTH_TESTS: 'ON',
            REALM_MONGODB_ENDPOINT: 'http://mongodb-realm:9090',
        ]
    }
    if (longRunningTests) {
        cmakeOptions << [
            CMAKE_CXX_FLAGS: '"-DTEST_DURATION=1"',
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        rlmNode('docker') {
            getArchive()

            def buildEnv = buildDockerEnv('testing.Dockerfile')

            def environment = environment()
            environment << 'UNITTEST_XML=unit-test-report.xml'
            environment << "UNITTEST_SUITE_NAME=Linux-${options.buildType}"
            if (options.useEncryption) {
                environment << 'UNITTEST_ENCRYPT_ALL=1'
            }

            def buildSteps = { String dockerArgs = "" ->
                buildEnv.inside(dockerArgs) {
                    withEnv(environment) {
                        try {
                            dir('build-dir') {
                                sh "cmake ${cmakeDefinitions} -G Ninja .."
                                runAndCollectWarnings(
                                    script: 'ninja',
                                    name: "linux-${options.buildType}-encrypt${options.enableEncryption}-BPNODESIZE_${options.maxBpNodeSize}",
                                    filters: warningFilters,
                                )
                                sh "${ctest_cmd}"
                            }
                        } finally {
                            junit testResults: 'build-dir/test/unit-test-report.xml'
                        }
                    }
                }
            }

            if (options.enableSync) {
                // stitch images are auto-published every day to our CI
                // see https://github.com/realm/ci/tree/master/realm/docker/mongodb-realm
                // we refrain from using "latest" here to optimise docker pull cost due to a new image being built every day
                // if there's really a new feature you need from the latest stitch, upgrade this manually
                withRealmCloud(version: dependencies.MDBREALM_TEST_SERVER_TAG) { networkName ->
                    buildSteps("--network=${networkName}")
                }

                if (options.dumpChangesetTransform) {
                    buildEnv.inside {
                        dir('build-dir/test') {
                            withEnv([
                                'UNITTEST_PROGRESS=1',
                                'UNITTEST_FILTER=Array_Example Transform_* EmbeddedObjects_*',
                                'UNITTEST_DUMP_TRANSFORM=changeset_dump',
                            ]) {
                                sh '''
                                    ./realm-sync-tests
                                    tar -zcvf changeset_dump.tgz changeset_dump
                                '''
                            }
                            withAWS(credentials: 'stitch-sync-s3', region: 'us-east-1') {
                                retry(20) {
                                    s3Upload file: 'changeset_dump.tgz', bucket: 'realm-test-artifacts', acl: 'PublicRead', path: "sync-transform-corpuses/${gitSha}/"
                                }
                            }
                        }
                    }
                }
            } else {
                buildSteps()
            }
        }
    }
}

def doCheckSanity(Map options = [:]) {
    def privileged = '';

    def cmakeOptions = [
        CMAKE_BUILD_TYPE: options.buildType,
        REALM_MAX_BPNODE_SIZE: options.maxBpNodeSize,
        REALM_ENABLE_SYNC: options.enableSync,
    ]

    if (options.sanitizeMode.contains('thread')) {
        cmakeOptions << [
            REALM_TSAN: "ON",
        ]
    }
    else if (options.sanitizeMode.contains('address')) {
        privileged = '--privileged'
        cmakeOptions << [
            REALM_ASAN: "ON",
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        rlmNode('docker') {
            getArchive()

            def environment = environment() + [
              'CC=clang',
              'CXX=clang++',
              'UNITTEST_XML=unit-test-report.xml',
              "UNITTEST_SUITE_NAME=Linux-${options.buildType}",
              "TSAN_OPTIONS=\"suppressions=${WORKSPACE}/test/tsan.suppress\""
            ]
            buildDockerEnv('testing.Dockerfile').inside(privileged) {
                withEnv(environment) {
                    try {
                        dir('build-dir') {
                            sh "cmake ${cmakeDefinitions} -G Ninja .."
                            runAndCollectWarnings(
                                script: 'ninja',
                                parser: "clang",
                                name: "linux-clang-${options.buildType}-${options.sanitizeMode}",
                                filters: warningFilters,
                            )
                            sh "${ctest_cmd}"
                        }

                    } finally {
                        junit testResults: 'build-dir/test/unit-test-report.xml'
                    }
                }
            }
        }
    }
}

def doBuildLinux(String buildType) {
    return {
        rlmNode('docker') {
            getSourceArchive()

            buildDockerEnv('packaging.Dockerfile').inside {
                sh """
                   rm -rf build-dir
                   mkdir build-dir
                   cd build-dir
                   cmake -DCMAKE_BUILD_TYPE=${buildType} -DREALM_NO_TESTS=1 -DREALM_VERSION="${gitDescribeVersion}" -G Ninja ..
                   ninja
                   cpack -G TGZ
                """
            }

            dir('build-dir') {
                archiveArtifacts("*.tar.gz")
                def stashName = "linux___${buildType}"
                stash includes:"*.tar.gz", name:stashName
                publishingStashes << stashName
            }
        }
    }
}

def doBuildLinuxClang(String buildType) {
    return {
        rlmNode('docker') {
            getArchive()

            def environment = environment() + [
              'CC=clang',
              'CXX=clang++'
            ]

            buildDockerEnv('testing.Dockerfile').inside {
                withEnv(environment) {
                    dir('build-dir') {
                        sh "cmake -D CMAKE_BUILD_TYPE=${buildType} -DREALM_NO_TESTS=1 -DREALM_VERSION=\"${gitDescribeVersion}\" -G Ninja .."
                        runAndCollectWarnings(
                            script: 'ninja',
                            parser: "clang",
                            name: "linux-clang-${buildType}",
                            filters: warningFilters,
                        )
                        sh 'cpack -G TGZ'
                    }
                }
            }

            dir('build-dir') {
                archiveArtifacts("*.tar.gz")
                def stashName = "linux___${buildType}"
                stash includes:"*.tar.gz", name:stashName
                publishingStashes << stashName
            }
        }
    }
}

def doAndroidBuildInDocker(String abi, String buildType, TestAction test = TestAction.None) {
    return {
        rlmNode('docker') {
            getArchive()
            def stashName = "android___${abi}___${buildType}"
            def buildDir = "build-${stashName}".replaceAll('___', '-')

            def buildEnv = buildDockerEnv('android.Dockerfile')

            def environment = environment()
            environment << 'UNITTEST_XML=/data/local/tmp/unit-test-report.xml'
            environment << 'UNITTEST_SUITE_NAME=android'
            def cmakeArgs = ''
            if (test == TestAction.None) {
                cmakeArgs = '-DREALM_NO_TESTS=ON'
            } else if (test.hasValue(TestAction.Build)) {
                // TODO: should we build sync tests, too?
                cmakeArgs = '-DREALM_ENABLE_SYNC=OFF -DREALM_FETCH_MISSING_DEPENDENCIES=ON -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON'
            }

            def doBuild = {
                buildEnv.inside {
                    runAndCollectWarnings(
                        parser: 'clang',
                        script: "tools/cross_compile.sh -o android -a ${abi} -t ${buildType} -v ${gitDescribeVersion} -f \"${cmakeArgs}\"",
                        name: "android-armeabi-${abi}-${buildType}",
                        filters: warningFilters,
                    )
                }
                if (test == TestAction.None) {
                    dir(buildDir) {
                        archiveArtifacts('realm-*.tar.gz')
                        stash includes: 'realm-*.tar.gz', name: stashName
                    }
                    androidStashes << stashName
                    if (gitTag) {
                        publishingStashes << stashName
                    }
                }
            }

            // if we want to run tests, let's spin up the emulator docker image first
            // it takes a while to warm up so we might as build in the mean time
            // otherwise, just run the build as is
            if (test.hasValue(TestAction.Run)) {
                docker.image('tracer0tong/android-emulator').withRun("-e ARCH=${abi}") { emulator ->
                    doBuild()
                    buildEnv.inside("--link ${emulator.id}:emulator") {
                        try {
                            sh """
                                cd ${buildDir}
                                adb connect emulator
                                timeout 30m adb wait-for-device
                                adb push test/realm-tests /data/local/tmp
                                adb push test/resources /data/local/tmp
                                adb shell 'cd /data/local/tmp; ${environment.join(' ')} ./realm-tests || echo __ADB_FAIL__' | tee adb.log
                                ! grep __ADB_FAIL__ adb.log
                            """
                        } finally {
                            sh 'adb pull /data/local/tmp/unit-test-report.xml'
                            junit testResults: 'unit-test-report.xml'
                        }
                    }
                }
            } else {
                doBuild()
            }
        }
    }
}

def doBuildWindows(String buildType, boolean isUWP, String platform, boolean runTests) {
    def cpackSystemName = "${isUWP ? 'UWP' : 'Windows'}-${platform}"
    def arch = platform.toLowerCase()
    if (arch == 'win32') {
      arch = 'x86'
    }
    if (arch == 'win64') {
      arch = 'x64'
    }
    def triplet = "${arch}-${isUWP ? 'uwp' : 'windows'}-static"

    def cmakeOptions = [
      CMAKE_GENERATOR_PLATFORM: platform,
      CMAKE_BUILD_TYPE: buildType,
      REALM_ENABLE_SYNC: "ON",
      CPACK_SYSTEM_NAME: cpackSystemName,
      REALM_VERSION: gitDescribeVersion,
    ]

     if (isUWP) {
      cmakeOptions << [
        CMAKE_SYSTEM_NAME: 'WindowsStore',
        CMAKE_SYSTEM_VERSION: '10.0',
      ]
    }

    if (!runTests) {
      cmakeOptions << [
        REALM_NO_TESTS: '1',
      ]
    } else {
        cmakeOptions << [
            VCPKG_MANIFEST_FEATURES: 'tests'
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=$v" }.join(' ')

    return {
        rlmNode('windows') {
            getArchive()

            dir('build-dir') {
                withAWS(credentials: 'tightdb-s3-ci', region: 'eu-west-1') {
                    withEnv(["VCPKG_BINARY_SOURCES=clear;x-aws,s3://vcpkg-binary-caches,readwrite"]) {
                        bat "\"${tool 'cmake'}\" ${cmakeDefinitions} .."
                    }
                }
                withEnv(["_MSPDBSRV_ENDPOINT_=${UUID.randomUUID().toString()}"]) {
                    runAndCollectWarnings(
                        parser: 'msbuild',
                        isWindows: true,
                        script: "\"${tool 'cmake'}\" --build . --config ${buildType}",
                        name: "windows-${platform}-${buildType}-${isUWP?'uwp':'nouwp'}",
                        filters: [excludeMessage('Publisher name .* does not match signing certificate subject'), excludeFile('query_flex.ll')] + warningFilters,
                    )
                }
                bat "\"${tool 'cmake'}\\..\\cpack.exe\" -C ${buildType} -D CPACK_GENERATOR=TGZ"
                archiveArtifacts('*.tar.gz')
                if (gitTag) {
                    def stashName = "windows___${platform}___${isUWP?'uwp':'nouwp'}___${buildType}"
                    stash includes:'*.tar.gz', name:stashName
                    publishingStashes << stashName
                }
            }
            if (runTests && !isUWP) {
                def prefix = "Windows-${platform}-${buildType}";
                def environment = environment() + [ "TMP=${env.WORKSPACE}\\temp", 'UNITTEST_NO_ERROR_EXITCODE=1' ]
                withEnv(environment + ["UNITTEST_XML=${WORKSPACE}\\core-results.xml", "UNITTEST_SUITE_NAME=${prefix}-core"]) {
                    dir("build-dir/test/${buildType}") {
                        bat '''
                          mkdir %TMP%
                          realm-tests.exe
                          rmdir /Q /S %TMP%
                        '''
                    }
                }
                if (arch == 'x86') {
                  // On 32-bit Windows we run out of address space when running
                  // the sync tests in parallel
                  environment << 'UNITTEST_THREADS=1'
                }
                withEnv(environment + ["UNITTEST_XML=${WORKSPACE}\\sync-results.xml", "UNITTEST_SUITE_NAME=${prefix}-sync"]) {
                    dir("build-dir/test/${buildType}") {
                        bat '''
                          mkdir %TMP%
                          realm-sync-tests.exe
                          rmdir /Q /S %TMP%
                        '''
                    }
                }

                withEnv(environment + ["UNITTEST_XML=${WORKSPACE}\\object-store-results.xml", "UNITTEST_SUITE_NAME=${prefix}-object-store"]) {
                    dir("build-dir/test/object-store/${buildType}") {
                        bat '''
                          mkdir %TMP%
                          realm-object-store-tests.exe
                          rmdir /Q /S %TMP%
                        '''
                    }
                }
                junit testResults: 'core-results.xml'
                junit testResults: 'sync-results.xml'
                junit testResults: 'object-store-results.xml'
            }
        }
    }
}

def doBuildMacOs(Map options = [:]) {
    def buildType = options.buildType;

    def cmakeOptions = [
        CMAKE_TOOLCHAIN_FILE: '$WORKSPACE/tools/cmake/xcode.toolchain.cmake',
        CMAKE_SYSTEM_NAME: 'Darwin',
        CPACK_SYSTEM_NAME: 'macosx',
        CMAKE_OSX_ARCHITECTURES: 'x86_64;arm64',
        CPACK_PACKAGE_DIRECTORY: '$WORKSPACE',
        REALM_ENABLE_SYNC: options.enableSync,
        REALM_VERSION: gitDescribeVersion
    ]
    if (!options.runTests) {
        cmakeOptions << [
            REALM_NO_TESTS: 'ON',
        ]
    }
    if (longRunningTests) {
        cmakeOptions << [
            CMAKE_CXX_FLAGS: '-DTEST_DURATION=1',
        ]
    }

    def cmakeDefinitions = cmakeOptions.collect { k,v -> "-D$k=\"$v\"" }.join(' ')

    return {
        rlmNode('osx') {
            getArchive()

            dir('build-macosx') {
                withEnv(['DEVELOPER_DIR=/Applications/Xcode-13.1.app/Contents/Developer/']) {
                    // This is a dirty trick to work around a bug in xcode
                    // It will hang if launched on the same project (cmake trying the compiler out)
                    // in parallel.
                    retry(3) {
                        timeout(time: 2, unit: 'MINUTES') {
                            sh "cmake ${cmakeDefinitions} -G Xcode .."
                        }
                    }

                    runAndCollectWarnings(
                        parser: 'clang',
                        script: "cmake --build . --config ${buildType} --target package -- ONLY_ACTIVE_ARCH=NO",
                        name: "xcode-macosx-${buildType}",
                        filters: warningFilters,
                    )
                }
            }
            withEnv(['DEVELOPER_DIR=/Applications/Xcode-13.1.app/Contents/Developer']) {
                runAndCollectWarnings(
                    parser: 'clang',
                    script: 'xcrun swift build',
                    name: "swift-build-macosx-${buildType}",
                    filters: warningFilters,
                )
                sh 'xcrun swift run ObjectStoreTests'
            }

            String tarball = "realm-${buildType}-${gitDescribeVersion}-macosx-*.tar.gz"
            archiveArtifacts tarball

            def stashName = "macosx___${buildType}"
            stash includes: tarball, name: stashName
            cocoaStashes << stashName
            publishingStashes << stashName

            if (options.runTests) {
                try {
                    def environment = environment()
                    environment << 'CTEST_OUTPUT_ON_FAILURE=1'
                    environment << "UNITTEST_XML=${WORKSPACE}/unit-test-report.xml"
                    environment << "UNITTEST_SUITE_NAME=macOS_${buildType}"

                    dir('build-macosx') {
                        withEnv(environment) {
                            sh "${ctest_cmd} -C ${buildType}"
                        }
                    }
                } finally {
                    junit testResults: 'unit-test-report.xml'
                }
            }
        }
    }
}

def doBuildApplePlatform(String platform, String buildType, boolean test = false) {
    return {
        rlmNode('osx') {
            getArchive()

            withEnv(['DEVELOPER_DIR=/Applications/Xcode-13.1.app/Contents/Developer/',
                     'XCODE_14_DEVELOPER_DIR=/Applications/Xcode-14.app/Contents/Developer/']) {
                sh "tools/build-apple-device.sh -p '${platform}' -c '${buildType}' -v '${gitDescribeVersion}'"

                if (test) {
                    dir('build-xcode-platforms') {
                        if (platform != 'iphonesimulator') error 'Testing is only available for iOS Simulator'
                        sh "xcodebuild -scheme CoreTests -configuration ${buildType} -sdk iphonesimulator -arch x86_64"
                        // sh "xcodebuild -scheme SyncTests -configuration ${buildType} -sdk iphonesimulator -arch x86_64 IPHONEOS_DEPLOYMENT_TARGET=13"
                        sh "xcodebuild -scheme ObjectStoreTests -configuration ${buildType} -sdk iphonesimulator -arch x86_64 IPHONEOS_DEPLOYMENT_TARGET=13"

                        def env = environment().collect { v -> "SIMCTL_CHILD_${v}" }
                        def resultFile = "${WORKSPACE}/core-test-report.xml"
                        withEnv(env + ["SIMCTL_CHILD_UNITTEST_XML=${resultFile}", "SIMCTL_CHILD_UNITTEST_SUITE_NAME=iOS-${buildType}-Core"]) {
                            sh "$WORKSPACE/tools/run-in-simulator.sh 'test/${buildType}-${platform}/realm-tests.app' 'io.realm.CoreTests' '${resultFile}'"
                        }
                        // Sync tests currently don't work on iOS because they require an unimplemented server feature
                        // resultFile = "${WORKSPACE}/sync-test-report.xml"
                        // withEnv(env + ["SIMCTL_CHILD_UNITTEST_XML=${resultFile}", "SIMCTL_CHILD_UNITTEST_SUITE_NAME=iOS-${buildType}-Sync"]) {
                        //     sh "$WORKSPACE/tools/run-in-simulator.sh 'test/${buildType}-${platform}/realm-sync-tests.app' 'io.realm.SyncTests' '${resultFile}'"
                        // }
                        resultFile = "${WORKSPACE}/object-store-test-report.xml"
                        withEnv(env + ["SIMCTL_CHILD_UNITTEST_XML=${resultFile}", "SIMCTL_CHILD_UNITTEST_SUITE_NAME=iOS-${buildType}-Object-Store"]) {
                            sh "$WORKSPACE/tools/run-in-simulator.sh 'test/object-store/${buildType}-${platform}/realm-object-store-tests.app' 'io.realm.ObjectStoreTests' '${resultFile}'"
                        }
                    }
                }
            }

            if (test) {
                junit testResults: 'core-test-report.xml'
                // junit testResults: 'sync-test-report.xml'
                junit testResults: 'object-store-test-report.xml'
            }

            String tarball = "realm-${buildType}-${gitDescribeVersion}-${platform}-devel.tar.gz";
            archiveArtifacts tarball

            def stashName = "${platform}___${buildType}"
            stash includes: tarball, name: stashName
            cocoaStashes << stashName
            publishingStashes << stashName
        }
    }
}

def doBuildCoverage() {
  return {
    rlmNode('docker') {
      getArchive()

      buildDockerEnv('testing.Dockerfile').inside {
        sh '''
          mkdir build
          cd build
          cmake -G Ninja -D REALM_COVERAGE=ON ..
          ninja
          cd ..
          lcov --no-external --capture --initial --directory . --output-file $WORKSPACE/coverage-base.info
          cd build/test
          ulimit -c unlimited
          UNITTEST_PROGRESS=1 ./realm-tests
          cd ../..
          lcov --no-external --directory . --capture --output-file $WORKSPACE/coverage-test.info
          lcov --add-tracefile $WORKSPACE/coverage-base.info --add-tracefile coverage-test.info --output-file $WORKSPACE/coverage-total.info
          lcov --remove $WORKSPACE/coverage-total.info '/usr/*' '$WORKSPACE/test/*' --output-file $WORKSPACE/coverage-filtered.info
          rm coverage-base.info coverage-test.info coverage-total.info
        '''
        withCredentials([[$class: 'StringBinding', credentialsId: 'codecov-token-core', variable: 'CODECOV_TOKEN']]) {
          sh 'curl -s https://codecov.io/bash | bash'
        }
      }
    }
  }
}

def environment() {
    return [
        "UNITTEST_SHUFFLE=1",
        "UNITTEST_PROGRESS=1"
    ]
}

def readGitTag() {
    def command = 'git describe --exact-match --tags HEAD'
    def returnStatus = sh(returnStatus: true, script: command)
    if (returnStatus != 0) {
        return null
    }
    return sh(returnStdout: true, script: command).trim()
}

def isCronJob() {
    def upstreams = currentBuild.getUpstreamBuilds()
    for(upstream in upstreams) {
        def upstreamProjectName = upstream.getFullProjectName()
        def isRealmCronBuild = upstreamProjectName == 'realm-core-cron'
        if(isRealmCronBuild)
            return true;
    }
    return false;
}

def isNightlyBuildNeeded() {
    def command = 'git log -1 --format=%cI'
    def lastCommitTime = sh(returnStdout: true, script:command).trim()
    def current_dt = java.time.LocalDateTime.now()
    def last_commit_dt = java.time.LocalDateTime.parse(lastCommitTime, java.time.format.DateTimeFormatter.ISO_DATE_TIME)
    echo "Last Commit Time: ${last_commit_dt}"
    return current_dt.getDayOfYear() - last_commit_dt.getDayOfYear() <= 1;
}

def setBuildName(newBuildName) {
    currentBuild.displayName = "${currentBuild.displayName} - ${newBuildName}"
}

def getArchive() {
    deleteDir()
    unstash 'core-source'

    // If the current node's clock is behind the clock of the node that stashed the sources originally
    // the CMake generated files will always be out-of-date compared to the source files,
    // which can lead Ninja into a loop.
    // Touching all source files to reset their timestamp relative to the current node works around that.
    if (isUnix()) {
        sh 'find . -type f -exec touch {} +'
    } else {
        powershell 'Get-ChildItem . * -recurse | ForEach-Object{$_.LastWriteTime = get-date}'
    }
}

def getSourceArchive() {
    checkout(
        [
          $class           : 'GitSCM',
          branches         : scm.branches,
          gitTool          : 'native git',
          extensions       : scm.extensions + [[$class: 'CleanCheckout'], [$class: 'CloneOption', depth: 0, noTags: false, reference: '', shallow: false],
                                               [$class: 'SubmoduleOption', disableSubmodules: false, parentCredentials: false, recursiveSubmodules: true,
                                                         reference: '', trackingSubmodules: false]],
          userRemoteConfigs: scm.userRemoteConfigs
        ]
    )
}

def buildDockerEnv(String dockerfile = 'Dockerfile', String extraArgs = '') {
    def buildEnv
    docker.withRegistry('https://ghcr.io', 'github-packages-token') {
        buildEnv = docker.build("ci/realm-core:${dockerfile}", ". -f ${dockerfile} ${extraArgs}")
    }
    return buildEnv
}

enum TestAction {
    None(0x0),
    Build(0x1),
    Run(0x3); // build and run

    private final long value;

    TestAction(long value) {
        this.value = value;
    }

    public boolean hasValue(TestAction value) {
        return (this.value & value.value) == value.value;
    }
}
