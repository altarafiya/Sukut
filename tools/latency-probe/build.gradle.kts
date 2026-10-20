plugins { id("com.android.application") }
android {
    namespace = "com.osama.sukut.latencyprobe"
    compileSdk = 36
    defaultConfig {
        applicationId = "com.osama.sukut.latencyprobe"
        minSdk = 29
        targetSdk = 36
        versionCode = 2
        versionName = "1.1"
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
