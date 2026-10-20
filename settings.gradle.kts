pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "Sukut"
include(":app")
// Standalone measurement tool; never included in Sukut's application APK.
include(":latencyProbe")
project(":latencyProbe").projectDir = file("tools/latency-probe")
