#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FscEmbeddedLogCallback)(const char* message, void* refcon);
typedef void (*FscEmbeddedPrefsReloadCallback)(void* refcon);

typedef struct FscEmbeddedHostConfig {
    int embedded_mode;
    int host_owns_menu;
    int host_owns_logging;
    const char* prefs_path_override;
    const char* profiles_dir_override;
    FscEmbeddedLogCallback log_callback;
    void* log_callback_refcon;
    FscEmbeddedPrefsReloadCallback prefs_reload_callback;
    void* prefs_reload_callback_refcon;
} FscEmbeddedHostConfig;

#if defined(FSC_EMBEDDED)
void FscEmbedded_SetHostConfig(const FscEmbeddedHostConfig* config);
void FscEmbedded_ResetHostConfig(void);
void FscEmbedded_Start(void);
void FscEmbedded_Stop(void);
void FscEmbedded_Enable(void);
void FscEmbedded_Disable(void);
void FscEmbedded_OnMessage(int inMessage);
void FscEmbedded_ReloadPrefs(void);
void FscEmbedded_ToggleWindow(void);
#endif

#ifdef __cplusplus
}
#endif
