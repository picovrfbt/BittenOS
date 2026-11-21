import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.bittenos.app"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.bittenos.app"
        minSdk = 26          // AudioTrack's low-latency mode arrived in 26
        targetSdk = 35
        // From version.json, which the app also reads from GitHub to know when there's a newer one
        val v = groovy.json.JsonSlurper().parse(rootProject.file("version.json")) as Map<*, *>
        versionCode = (v["code"] as Number).toInt()
        versionName = v["name"].toString()
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            // Sideloaded onto one phone, so the debug key is all the signing it needs.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}

// ---- the mask update the app carries -----------------------------------------------------------
// Firmware, web pages and built-in faces from BittenOS-Controller, packed into the app's assets so
// the phone can update the mask with no internet. A backup to flashing from the PC. The build stops
// if the firmware is older than its source, so a stale firmware never ships.
val maskRoot = rootProject.file("../BittenOS-Controller")
val maskOut = layout.buildDirectory.dir("generated/maskAssets")
val bundleMask = tasks.register("bundleMask") {
    val fw = File(maskRoot, ".pio/build/esp32-s3/firmware.bin")
    val data = File(maskRoot, "data")
    val ver = File(maskRoot, "src/version.h")
    inputs.file(fw); inputs.dir(data); inputs.file(ver); inputs.dir(File(maskRoot, "src"))
    outputs.dir(maskOut)
    doLast {
        val newest = File(maskRoot, "src").walkTopDown().filter { it.isFile }.maxOf { it.lastModified() }
        if (!fw.isFile) throw GradleException("No mask firmware: build it first (python -m platformio run in BittenOS-Controller)")
        if (fw.lastModified() < newest) throw GradleException("The mask firmware is older than its source: rebuild it first")
        val h = ver.readText()
        val code = Regex("#define BITTENOS_FW ([0-9]+)").find(h)!!.groupValues[1]
        val name = Regex("#define BITTENOS_FW_NAME \"([^\"]*)\"").find(h)!!.groupValues[1]
        val out = maskOut.get().asFile.resolve("mask").apply { deleteRecursively(); mkdirs() }
        fw.copyTo(out.resolve("firmware.bin"))
        out.resolve("version.txt").writeText("$code\n$name\n")
        // Same rule as webNameOk() in main.cpp: page files only, never settings
        val pageOk = Regex("[A-Za-z0-9._-]{1,40}")
        val ends = listOf(".html", ".js", ".js.gz", ".css", ".png", ".svg", ".ico")
        ZipOutputStream(out.resolve("web.zip").outputStream()).use { z ->
            data.listFiles()!!.filter { f -> f.isFile && pageOk.matches(f.name) && !f.name.startsWith(".") &&
                (f.name == "manifest.json" || ends.any { f.name.endsWith(it) }) }.sortedBy { it.name }.forEach { f ->
                z.putNextEntry(ZipEntry(f.name)); z.write(f.readBytes()); z.closeEntry()
            }
            File(data, "anims").listFiles()!!.filter { it.name.endsWith(".json") }.sortedBy { it.name }.forEach { f ->
                z.putNextEntry(ZipEntry("anims/" + f.name)); z.write(f.readBytes()); z.closeEntry()
            }
        }
        println("Bundled mask v$name ($code)")
    }
}
android.sourceSets.getByName("main").assets.srcDir(maskOut)
tasks.named("preBuild") { dependsOn(bundleMask) }
