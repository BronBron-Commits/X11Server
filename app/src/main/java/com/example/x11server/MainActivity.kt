package com.example.x11server

import android.app.Activity
import android.os.Bundle

class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        nativeInit()
    }

    override fun onPause() {
        super.onPause()
        nativePause()
    }

    override fun onResume() {
        super.onResume()
        nativeResume()
    }

    private external fun nativeInit()
    private external fun nativePause()
    private external fun nativeResume()

    companion object {
        init {
            System.loadLibrary("x11server")
        }
    }
}