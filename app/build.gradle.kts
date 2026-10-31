import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val releaseSigningPath = providers.gradleProperty("sukutSigningProperties")
    .orElse(providers.environmentVariable("SUKUT_ANDROID_SIGNING_PROPERTIES")).orNull
val releaseSigningValues = Properties().apply {
    releaseSigningPath?.let { path ->
        val propertiesFile = file(path)
        require(propertiesFile.isFile) { "Android release signing properties file was not found." }
        propertiesFile.inputStream().use { load(it) }
    }
}
fun signingValue(name: String): String = requireNotNull(releaseSigningValues.getProperty(name)) {
    "Missing Android signing property: $name"
}

android {
    namespace = "com.osama.cleanwave"
    compileSdk = 36
    testBuildType = "engine"

    defaultConfig {
        applicationId = "com.altarafiyah.sukut"
        minSdk = 29
        targetSdk = 36
        versionCode = 56
        versionName = "0.4.2"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += "arm64-v8a" }
    }

    buildFeatures {
        buildConfig = true
        aidl = true
    }

    signingConfigs {
        create("production") {
            if (releaseSigningPath != null) {
                storeFile = file(signingValue("storeFile"))
                storePassword = signingValue("storePassword")
                keyAlias = signingValue("keyAlias")
                keyPassword = signingValue("keyPassword")
                storeType = releaseSigningValues.getProperty("storeType", "PKCS12")
            }
        }
    }

    buildTypes {
        getByName("debug") {
            buildConfigField("boolean", "ENGINE_RUNTIME", "false")
        }
        create("engine") {
            initWith(getByName("release"))
            isDebuggable = false
            isJniDebuggable = false
            signingConfig = signingConfigs.getByName("production")
            buildConfigField("boolean", "ENGINE_RUNTIME", "true")
            matchingFallbacks += listOf("release")
            // Remove unreachable legacy app code, without rewriting arithmetic,
            // changing names or shrinking UI resources used by setup/reflection.
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android.txt"), "proguard-engine.pro")
            testProguardFiles("proguard-tests.pro")
        }
        getByName("release") {
            buildConfigField("boolean", "ENGINE_RUNTIME", "false")
        }
    }

    sourceSets {
        getByName("debug").assets.srcDir("src/legacy/assets")
        getByName("release").assets.srcDir("src/legacy/assets")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }
    packaging { jniLibs.useLegacyPackaging = true }
    // The in-app language picker works offline and must retain both locales in Play installs.
    bundle { language { enableSplit = false } }
    androidResources { noCompress += listOf("onnx", "tflite") }
}

dependencies {
    // The interface uses framework Views only (no AppCompat/Material runtime).
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:6.1")
    compileOnly("com.microsoft.onnxruntime:onnxruntime-android:1.23.2")
    "debugImplementation"("com.microsoft.onnxruntime:onnxruntime-android:1.23.2")
    "releaseImplementation"("com.microsoft.onnxruntime:onnxruntime-android:1.23.2")
    implementation("com.github.wendykierp:JTransforms:3.1")
    "engineImplementation"(files("vendor/litert-1.0.1-arm64.aar"))
    "engineImplementation"(files("vendor/litert-api-1.0.1.aar"))
    testImplementation("junit:junit:4.13.2")
    testImplementation("com.microsoft.onnxruntime:onnxruntime:1.23.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
}

tasks.withType<Test>().configureEach {
    systemProperty("cleanwave.models", file("src/legacy/assets").absolutePath)
    systemProperty("cleanwave.research", rootProject.file("work/quality-upgrade").absolutePath)
    inputs.dir(file("src/legacy/assets")).withPropertyName("qualityModels")
    inputs.files(rootProject.fileTree("work/quality-upgrade/fixtures") {
        include("*-mixture.wav", "*-vocals.wav", "clean-speech-48k.wav", "dialogue-music-48k.wav")
    }).withPropertyName("privateQualityFixtures")
}

// Keep device-test annotations resolvable from this workspace's offline cache.
// The production APK has no AndroidX UI/annotation dependency.
configurations.matching { it.name.contains("AndroidTest") }.configureEach {
    resolutionStrategy.dependencySubstitution {
        substitute(module("androidx.annotation:annotation"))
            .using(module("androidx.annotation:annotation-jvm:1.9.1"))
    }
}
