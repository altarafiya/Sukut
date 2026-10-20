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
        applicationId = "com.osama.sukut"
        minSdk = 29
        targetSdk = 36
        versionCode = 37
        versionName = "0.3.31"
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
    androidResources { noCompress += listOf("onnx", "tflite") }
}

dependencies {
    implementation("dev.rikka.shizuku:api:13.1.5")
    implementation("dev.rikka.shizuku:provider:13.1.5")
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:6.1")
    compileOnly("com.microsoft.onnxruntime:onnxruntime-android:1.23.2")
    "debugImplementation"("com.microsoft.onnxruntime:onnxruntime-android:1.23.2")
    "releaseImplementation"("com.microsoft.onnxruntime:onnxruntime-android:1.23.2")
    implementation("com.github.wendykierp:JTransforms:3.1")
    "engineImplementation"(files("vendor/tensorflow-lite-2.16.1-neutral.aar"))
    "engineImplementation"("org.tensorflow:tensorflow-lite-api:2.16.1")
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
