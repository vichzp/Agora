package com.newoether.agora.data

import com.newoether.agora.ui.chat.message.localBackendLabel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class LocalRunStatsTest {
    @Test
    fun passesSumPromptWorkAndKeepTheFirstTokenTime() {
        val first = LocalRunStats("GPUOpenCL", promptProcessedTokens = 600, promptCachedTokens = 5900, promptMs = 1500, firstTokenMs = 2100)
        val second = LocalRunStats("GPUOpenCL", promptProcessedTokens = 400, promptCachedTokens = 6500, promptMs = 1000, firstTokenMs = 900)
        val total = first.plus(second)
        assertEquals(1000, total.promptProcessedTokens)
        assertEquals(12400, total.promptCachedTokens)
        assertEquals(2500L, total.promptMs)
        assertEquals(2100L, total.firstTokenMs)
        assertEquals(400.0, total.promptTokensPerSecond!!, 0.001)
    }

    @Test
    fun fullyCachedPromptHasNoSpeed() {
        assertNull(LocalRunStats("CPU", promptProcessedTokens = 0, promptMs = 0).promptTokensPerSecond)
    }

    @Test
    fun backendLabels() {
        assertEquals("GPU (OpenCL)", localBackendLabel("GPUOpenCL"))
        assertEquals("CPU", localBackendLabel("CPU"))
        assertEquals("CPU: weights unsupported on GPU", localBackendLabel("CPU: weights unsupported on GPU"))
    }
}
