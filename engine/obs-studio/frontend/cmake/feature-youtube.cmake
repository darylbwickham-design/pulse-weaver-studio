# Release packaging supplies the public desktop registration. Existing local
# registrations remain supported without exposing developer fields in the UI.
if(TARGET OBS::browser-panels)
  target_sources(
    obs-studio
    PRIVATE
      dialogs/OBSYoutubeActions.cpp
      dialogs/OBSYoutubeActions.hpp
      docks/YouTubeAppDock.cpp
      docks/YouTubeAppDock.hpp
      docks/YouTubeChatDock.cpp
      docks/YouTubeChatDock.hpp
      forms/OBSYoutubeActions.ui
      oauth/YoutubeAuth.cpp
      oauth/YoutubeAuth.hpp
      utility/YoutubeApiWrappers.cpp
      utility/YoutubeApiWrappers.hpp
  )

  target_enable_feature(obs-studio "YouTube API connection" YOUTUBE_ENABLED)
else()
  target_disable_feature(obs-studio "YouTube API connection")
  set(YOUTUBE_SECRET_HASH 0)
  set(YOUTUBE_CLIENTID_HASH 0)
endif()

if(NOT YOUTUBE_SECRET)
  set(YOUTUBE_SECRET "")
  set(YOUTUBE_SECRET_HASH 0)
endif()
