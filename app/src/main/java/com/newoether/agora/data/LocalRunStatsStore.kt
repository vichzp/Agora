package com.newoether.agora.data

import android.content.Context
import androidx.compose.runtime.Immutable
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File

/** On-device run details of one assistant reply, summed over its provider passes. */
@Immutable
@Serializable
data class LocalRunStats(
    val backend: String,
    val promptProcessedTokens: Int = 0,
    val promptCachedTokens: Int = 0,
    val promptMs: Long = 0,
    val firstTokenMs: Long? = null,
) {
    val promptTokensPerSecond: Double?
        get() = if (promptMs > 0 && promptProcessedTokens > 0) {
            promptProcessedTokens * 1000.0 / promptMs
        } else {
            null
        }

    fun plus(next: LocalRunStats): LocalRunStats = copy(
        backend = next.backend,
        promptProcessedTokens = promptProcessedTokens + next.promptProcessedTokens,
        promptCachedTokens = promptCachedTokens + next.promptCachedTokens,
        promptMs = promptMs + next.promptMs,
        firstTokenMs = firstTokenMs ?: next.firstTokenMs,
    )
}

/**
 * Sidecar for [LocalRunStats], keyed by message id. Kept out of the chat database on purpose so
 * the fork's schema stays identical to upstream's; replies without an entry show no such rows.
 * Only the newest [MAX_ENTRIES] replies are kept.
 */
object LocalRunStatsStore {
    private const val FILE_NAME = "local-run-stats.json"
    private const val MAX_ENTRIES = 2000

    private val json = Json { ignoreUnknownKeys = true }
    private var file: File? = null
    private val entries = LinkedHashMap<String, LocalRunStats>()
    private var loaded = false

    fun initialize(context: Context) = synchronized(this) {
        file = File(context.filesDir, FILE_NAME)
    }

    fun get(messageId: String): LocalRunStats? = synchronized(this) {
        load()
        entries[messageId]
    }

    fun record(messageId: String, stats: LocalRunStats) = synchronized(this) {
        load()
        val merged = entries.remove(messageId)?.plus(stats) ?: stats
        entries[messageId] = merged
        while (entries.size > MAX_ENTRIES) entries.remove(entries.keys.first())
        save()
    }

    private fun load() {
        if (loaded) return
        loaded = true
        val source = file?.takeIf(File::isFile) ?: return
        runCatching {
            json.decodeFromString<Map<String, LocalRunStats>>(source.readText())
        }.getOrNull()?.let(entries::putAll)
    }

    private fun save() {
        val target = file ?: return
        runCatching {
            val temporary = File(target.parentFile, "$FILE_NAME.tmp")
            temporary.writeText(json.encodeToString(entries.toMap()))
            temporary.renameTo(target)
        }
    }
}
