package com.newoether.agora.api.local

import android.os.Build

/**
 * Best-guess accelerator per chipset for on-device models; anything not listed runs on the CPU.
 *
 * llama.cpp's OpenCL backend is maintained by Qualcomm for Adreno 7xx/8xx GPUs. Public reports
 * have Mali and Xclipse GPUs (Dimensity, Exynos, Tensor) running slower than the CPU under
 * Vulkan, so those stay on the CPU. Keys are Build.SOC_MODEL values.
 */
internal object LocalAcceleratorTable {
    const val CPU = "CPU"
    const val OPENCL = "OpenCL"

    private val preferred = mapOf(
        "SM8850" to OPENCL, // Snapdragon 8 Elite Gen 5, Adreno 840
        "SM8750" to OPENCL, // 8 Elite, Adreno 830
        "SM8750P" to OPENCL,
        "SM8735" to OPENCL, // 8s Gen 4, Adreno 825
        "SM8650" to OPENCL, // 8 Gen 3, Adreno 750
        "SM8650P" to OPENCL,
        "SM8635" to OPENCL, // 8s Gen 3, Adreno 735
        "SM8550" to OPENCL, // 8 Gen 2, Adreno 740
        "SM8550P" to OPENCL,
        "SM7675" to OPENCL, // 7+ Gen 3, Adreno 732
        "SM7635" to OPENCL, // 7s Gen 3, Adreno 810
    )

    fun preferredBackend(socModel: String = currentSocModel()): String =
        preferred[socModel.trim().uppercase()] ?: CPU

    fun currentSocModel(): String =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL else Build.HARDWARE
}
