/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.example.helloaar

import android.annotation.SuppressLint
import android.webkit.JavascriptInterface
import android.app.Activity
import android.content.res.AssetFileDescriptor
import android.content.res.AssetManager
import android.graphics.Color
import android.media.MediaPlayer
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.*
import android.view.Choreographer.FrameCallback
import android.view.SurfaceHolder.Callback
import android.webkit.WebView
import android.view.ViewGroup.LayoutParams
import android.view.ViewGroup.LayoutParams.MATCH_PARENT

class JSInterface(val activity: MainActivity) {
    @JavascriptInterface
    fun sendInput(x: Int, y: Int, state: Int) {
        activity.onInput(x.toFloat(), y.toFloat(), state)
    }
}

class MainActivity : Activity(), Callback, FrameCallback {
    val mediaPlayers= Array(5) { MediaPlayer() }
    val audioTracks = mutableMapOf<String, AssetFileDescriptor>()

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        /*
         * Retrieve our TextView and set its content.
         * the text is retrieved by calling a native
         * function.
         */

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.decorView.windowInsetsController?.hide(WindowInsets.Type.navigationBars())
        }

        val surfaceView = SurfaceView(this)
        setContentView(surfaceView)
//        surfaceView.setZOrderOnTop(true)

        surfaceView.holder.addCallback(this)

        val myWebView = WebView(this)
        myWebView.isVerticalFadingEdgeEnabled = false
        myWebView.settings.javaScriptEnabled = true
        myWebView.setBackgroundColor(Color.TRANSPARENT)
        myWebView.addJavascriptInterface(JSInterface(this), "webkit")

        addContentView(myWebView, LayoutParams(MATCH_PARENT, MATCH_PARENT))
        myWebView.loadUrl("file:///android_asset/index.html")
    }

    fun playAudio(name: String) {
        val afd : AssetFileDescriptor
        if(audioTracks.containsKey(name)) {
            afd = audioTracks.getValue(name)
        }else{
            afd = assets.openFd(name)
            audioTracks[name] = afd
        }

        for(i in mediaPlayers){
            if(!i.isPlaying) {
                i.reset()
                i.setDataSource(afd.fileDescriptor, afd.startOffset, afd.length)
                i.prepare()
                i.start()

                return
            }
        }
    }

    /*
     * A native method that is implemented by the
     * 'hello-jni' native library, which is packaged
     * with this application.
     */
    external fun onStart(assetManager: AssetManager, surface: Surface): Int
    external fun onResize(width: Int, height: Int): Int
    external fun onUpdate(time: Long): Int
    external fun onInput(x: Float, y: Float, state: Int): Int
    external fun onFinish(): Int

    /*
     * This is another native method declaration that is *not*
     * implemented by 'hello-jni'. This is simply to show that
     * you can declare as many native methods in your Java code
     * as you want, their implementation is searched in the
     * currently loaded native libraries only the first time
     * you call them.
     *
     * Trying to call this function will result in a
     * java.lang.UnsatisfiedLinkError exception !
     */
//    external fun unimplementedStringFromJNI(): String?

    companion object {
        /*
         * this is used to load the 'hello-jni' library on application
         * startup. The library has already been unpacked into
         * /data/data/com.example.hellojni/lib/libhello-jni.so
         * at the installation time by the package manager.
         */
        init {
            System.loadLibrary("Test")
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
//        TODO("Not yet implemented")
        onStart(assets, holder.surface)
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
//        TODO("Not yet implemented")
        onResize(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
//        TODO("Not yet implemented")
        Choreographer.getInstance().removeFrameCallback(this)
        onFinish()
    }

    override fun doFrame(frameTimeNanos: Long) {
//        TODO("Not yet implemented")
        onUpdate(frameTimeNanos)
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
//        Log.i("Thien", "${event.x} ${event.y}");

        onInput(event.x, event.y, when (event.action) {
            MotionEvent.ACTION_DOWN -> 0
            MotionEvent.ACTION_MOVE -> 1
            else -> 3
        })

        return true
    }
}