package com.newoether.agora.api.local

import org.junit.Assert.assertEquals
import org.junit.Test

class LocalAcceleratorTableTest {
    @Test
    fun recentSnapdragonsPreferOpenCl() {
        for (soc in listOf("SM8735", "SM8750", "sm8650", " SM8550P ", "SM7635")) {
            assertEquals(soc, LocalAcceleratorTable.OPENCL, LocalAcceleratorTable.preferredBackend(soc))
        }
    }

    @Test
    fun otherChipsStayOnCpu() {
        for (soc in listOf("MT6989", "MT6991", "s5e9945", "zuma", "zumapro", "SM8450", "", "unknown")) {
            assertEquals(soc, LocalAcceleratorTable.CPU, LocalAcceleratorTable.preferredBackend(soc))
        }
    }
}
