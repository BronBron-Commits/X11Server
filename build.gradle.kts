// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.android) apply false
}

tasks.register<Exec>("buildTermuxUserland") {
    val scriptPath = "${rootDir}/tools/termux-build/build_termux_packages.ps1"
    val osName = System.getProperty("os.name").lowercase()
    if (osName.contains("windows")) {
        commandLine(
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            scriptPath
        )
    } else {
        commandLine("bash", "-lc", "${rootDir}/tools/termux-build/build_termux_packages.sh")
    }
    doLast {
        val zipPath = file("${rootDir}/app/src/main/assets/termux-root.zip")
        if (!zipPath.exists()) {
            throw GradleException(
                "termux-root.zip was not created. Check WSL/termux-packages build output. " +
                    "Expected at: ${zipPath.absolutePath}"
            )
        }
    }
}