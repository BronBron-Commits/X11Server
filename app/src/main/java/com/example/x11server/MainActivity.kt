package com.example.x11server

import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.util.Log
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.inputmethod.InputMethodManager
import android.widget.Button
import android.widget.TextView
import com.termux.terminal.TerminalSession
import com.termux.terminal.TerminalSessionClient
import com.termux.view.TerminalView
import com.termux.view.TerminalViewClient
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipInputStream

class MainActivity : Activity(), TerminalSessionClient, TerminalViewClient {
    private lateinit var terminalView: TerminalView
    private lateinit var terminalStatus: TextView
    private lateinit var restartX11Button: Button

    private var terminalSession: TerminalSession? = null
    private val baseFontSizeSp = 14
    private var terminalFontSizeSp = baseFontSizeSp
    private var currentHomeDir: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        terminalView = findViewById(R.id.terminal_view)
        terminalStatus = findViewById(R.id.terminal_status)
        restartX11Button = findViewById(R.id.terminal_restart_x11)

        terminalView.setTerminalViewClient(this)
        terminalView.setTextSize(terminalFontSizeSp)
        terminalView.requestFocus()
        terminalView.setOnTouchListener { _, event ->
            if (event.action == MotionEvent.ACTION_UP) {
                showKeyboardForTerminal()
            }
            false
        }

        restartX11Button.setOnClickListener {
            nativePause()
            nativeResume()
        }

        ensureAllFilesAccess()
        ensureUserlandExtracted()
        startTerminalSession()
        nativeInit()
    }

    override fun onResume() {
        super.onResume()
        nativeResume()
        val desiredHome = resolveHomeDir()
        if (terminalSession != null && currentHomeDir != desiredHome) {
            terminalSession?.finishIfRunning()
            terminalSession = null
            startTerminalSession()
        }
        terminalView.setTerminalCursorBlinkerState(true, true)
    }

    override fun onPause() {
        terminalView.setTerminalCursorBlinkerState(false, false)
        nativePause()
        super.onPause()
    }

    override fun onDestroy() {
        super.onDestroy()
        terminalSession?.finishIfRunning()
        terminalSession = null
    }

    private fun startTerminalSession() {
        if (terminalSession != null) {
            return
        }
        val homeDir = resolveHomeDir()
        currentHomeDir = homeDir
        val usrDir = File(filesDir, "usr")
        val usrBin = File(usrDir, "bin")
        if (!usrBin.exists()) {
            usrBin.mkdirs()
        }
        val env = arrayOf(
            "TERM=xterm-256color",
            "LANG=C.UTF-8",
            "HOME=$homeDir",
            "PATH=${usrBin.absolutePath}:/system/bin:/system/xbin"
        )
        terminalSession = TerminalSession(
            "/system/bin/sh",
            homeDir,
            emptyArray(),
            env,
            2000,
            this
        )
        terminalSession?.let { terminalView.attachSession(it) }
        if (!Environment.isExternalStorageManager()) {
            terminalStatus.text = "All files access not granted. Using app files."
        }
    }

    private external fun nativeInit()
    private external fun nativePause()
    private external fun nativeResume()

    companion object {
        init {
            System.loadLibrary("x11server")
        }
    }

    override fun onTextChanged(changedSession: TerminalSession) {
        if (changedSession == terminalSession) {
            terminalView.onScreenUpdated()
        }
    }

    override fun onTitleChanged(changedSession: TerminalSession) {
        if (changedSession == terminalSession) {
            val title = changedSession.title
            if (!title.isNullOrBlank()) {
                runOnUiThread { this.title = title }
            }
        }
    }

    override fun onSessionFinished(finishedSession: TerminalSession) {
        if (finishedSession == terminalSession) {
            terminalStatus.text = "Terminal exited"
        }
    }

    override fun onCopyTextToClipboard(session: TerminalSession, text: String) {
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText("terminal", text))
    }

    override fun onPasteTextFromClipboard(session: TerminalSession) {
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = clipboard.primaryClip
        val item = clip?.getItemAt(0)
        val text = item?.coerceToText(this)?.toString() ?: return
        terminalSession?.write(text)
    }

    override fun onBell(session: TerminalSession) {
        // No-op
    }

    override fun onColorsChanged(session: TerminalSession) {
        terminalView.onScreenUpdated()
    }

    override fun onTerminalCursorStateChange(state: Boolean) {
        terminalView.setTerminalCursorBlinkerState(state, true)
    }

    override fun getTerminalCursorStyle(): Int? {
        return null
    }

    override fun logError(tag: String, message: String) {
        Log.e(tag, message)
    }

    override fun logWarn(tag: String, message: String) {
        Log.w(tag, message)
    }

    override fun logInfo(tag: String, message: String) {
        Log.i(tag, message)
    }

    override fun logDebug(tag: String, message: String) {
        Log.d(tag, message)
    }

    override fun logVerbose(tag: String, message: String) {
        Log.v(tag, message)
    }

    override fun logStackTraceWithMessage(tag: String, message: String, e: Exception) {
        Log.e(tag, message, e)
    }

    override fun logStackTrace(tag: String, e: Exception) {
        Log.e(tag, e.message, e)
    }

    override fun onScale(scale: Float): Float {
        val newSize = (baseFontSizeSp * scale).toInt().coerceIn(10, 28)
        if (newSize != terminalFontSizeSp) {
            terminalFontSizeSp = newSize
            terminalView.setTextSize(terminalFontSizeSp)
        }
        return terminalFontSizeSp.toFloat() / baseFontSizeSp.toFloat()
    }

    override fun onSingleTapUp(e: MotionEvent) {
        showKeyboardForTerminal()
    }

    override fun shouldBackButtonBeMappedToEscape(): Boolean {
        return false
    }

    override fun shouldEnforceCharBasedInput(): Boolean {
        return true
    }

    override fun shouldUseCtrlSpaceWorkaround(): Boolean {
        return false
    }

    override fun isTerminalViewSelected(): Boolean {
        return terminalView.hasFocus()
    }

    override fun copyModeChanged(copyMode: Boolean) {
        // No-op
    }

    override fun onKeyDown(keyCode: Int, e: KeyEvent, session: TerminalSession): Boolean {
        return false
    }

    override fun onKeyUp(keyCode: Int, e: KeyEvent): Boolean {
        return false
    }

    override fun onLongPress(event: MotionEvent): Boolean {
        return false
    }

    override fun readControlKey(): Boolean {
        return false
    }

    override fun readAltKey(): Boolean {
        return false
    }

    override fun readShiftKey(): Boolean {
        return false
    }

    override fun readFnKey(): Boolean {
        return false
    }

    override fun onCodePoint(codePoint: Int, ctrlDown: Boolean, session: TerminalSession): Boolean {
        return false
    }

    override fun onEmulatorSet() {
        terminalView.setTerminalCursorBlinkerState(true, true)
        terminalSession?.pid?.let { pid ->
            terminalStatus.text = "PID $pid"
        }
    }

    private fun showKeyboardForTerminal() {
        terminalView.requestFocus()
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imm.showSoftInput(terminalView, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun resolveHomeDir(): String {
        return if (Environment.isExternalStorageManager()) {
            Environment.getExternalStorageDirectory().absolutePath
        } else {
            filesDir?.absolutePath ?: "/"
        }
    }

    private fun ensureAllFilesAccess() {
        if (Environment.isExternalStorageManager()) {
            return
        }
        val intent = Intent(
            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
            Uri.parse("package:$packageName")
        )
        startActivity(intent)
    }

    private fun ensureUserlandExtracted() {
        val usrDir = File(filesDir, "usr")
        val marker = File(usrDir, ".installed")
        if (marker.exists()) {
            return
        }
        try {
            assets.open("termux-root.zip").use { input ->
                ZipInputStream(input).use { zip ->
                    var entry = zip.nextEntry
                    while (entry != null) {
                        val outPath = File(filesDir, entry.name)
                        if (entry.isDirectory) {
                            outPath.mkdirs()
                        } else {
                            outPath.parentFile?.mkdirs()
                            FileOutputStream(outPath).use { out ->
                                val buffer = ByteArray(8192)
                                var read = zip.read(buffer)
                                while (read > 0) {
                                    out.write(buffer, 0, read)
                                    read = zip.read(buffer)
                                }
                            }
                        }
                        zip.closeEntry()
                        entry = zip.nextEntry
                    }
                }
            }
            usrDir.mkdirs()
            marker.writeText("ok")
        } catch (ex: Exception) {
            Log.w("Terminal", "No bundled userland found: ${ex.message}")
        }
    }
}