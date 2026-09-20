################################################################################
# CryGame -- the game module.
#
# THIS IS THE MODULE THAT MAKES IT A GAME
#
# Everything ported before it is engine: the system, the script system, the
# 3D engine's front end, the renderer. CryGame is what owns players, vehicles,
# weapons, the UI, mission logic and level loading -- and it was absent from
# this build entirely until now. Not excluded, not stubbed: it was never in
# add_subdirectory, never in a module list, and never covered by a compile
# census. That is the single reason the browser host has only ever shown test
# geometry.
#
# All 93 translation units compile. It took three things and no more:
#
#   * 104 case-sensitive include corrections. tools/fix_includes.py already
#     existed for exactly this and had never been pointed here.
#   * Two header declarations MSVC 7.1 accepted and standard C++ does not --
#     a member qualified with its own class name, and a "friend class" used
#     as though it were a forward declaration. Both headers are included
#     nearly everywhere in the module, so the two lines unblocked dozens of
#     units.
#   * Nine files with the usual MSVC-permissiveness residue: addresses of
#     temporaries, a Win32 GetCurrentTime, an imagehlp directory call, and a
#     pointer squeezed into an int.
#
# WHAT IT STILL NEEDS
#
# A game needs assets. Far Cry's are several GB of .pak, they are not in this
# tree, and they are not redistributable -- so a level cannot be loaded from
# this repository alone no matter how much of the code works.
################################################################################

cry_add_module(CryGame
    SOURCES
    AIHandler.cpp
    AdvCamSystem.cpp
    BitStream_Base.cpp
    BitStream_Compressed.cpp
    BugsFlock.cpp
    CryGame.cpp
    EntityClassRegistry.cpp
    Flock.cpp
    Game.cpp
    GameActionInput.cpp
    GameCallbacks.cpp
    GameClientServer.cpp
    GameDevMode.cpp
    GameEquipPacks.cpp
    GameEvents.cpp
    GameLoading.cpp
    GameMemStats.cpp
    GameMisc.cpp
    GameMods.cpp
    GameMovieUser.cpp
    GameRadar.cpp
    GameTagPoints.cpp
    IngameDialog.cpp
    NetEntityInfo.cpp
    ScriptObjectAI.cpp
    ScriptObjectAdvCamSystem.cpp
    ScriptObjectBoids.cpp
    ScriptObjectClient.cpp
    ScriptObjectGame.cpp
    ScriptObjectInput.cpp
    ScriptObjectLanguage.cpp
    ScriptObjectPlayer.cpp
    ScriptObjectRenderer.cpp
    ScriptObjectServer.cpp
    ScriptObjectServerSlot.cpp
    ScriptObjectSpectator.cpp
    ScriptObjectStream.cpp
    ScriptObjectSynched2DTable.cpp
    ScriptObjectUI.cpp
    ScriptObjectVehicle.cpp
    ScriptObjectWeaponClass.cpp
    ScriptTimerMgr.cpp
    Spectator.cpp
    StdAfx.cpp
    StringTableMgr.cpp
    Synched2DTable.cpp
    SynchedRandomSeed.cpp
    TimeDemoRecorder.cpp
    UIButton.cpp
    UICVars.cpp
    UICheckBox.cpp
    UIComboBox.cpp
    UIDivX_Video.cpp
    UIEditBox.cpp
    UIHud.cpp
    UIListView.cpp
    UIMenu.cpp
    UIScreen.cpp
    UIScrollBar.cpp
    UIStatic.cpp
    UISystem.cpp
    UIVideoPanel.cpp
    UIWidget.cpp
    WeaponClass.cpp
    WeaponSystemEx.cpp
    XArea.cpp
    XClient.cpp
    XClientSnapshot.cpp
    XDemoMgr.cpp
    XEntityProcessingCmd.cpp
    XGUIControl.cpp
    XNetwork.cpp
    XNetworkStats.cpp
    XObjectProxy.cpp
    XPlayer.cpp
    XPlayerCamera.cpp
    XPlayerLight.cpp
    XPuppetProxy.cpp
    XServer.cpp
    XServerRules.cpp
    XServerSlot.cpp
    XSnapshot.cpp
    XSurfaceMgr.cpp
    XSystemBase.cpp
    XSystemClient.cpp
    XSystemDummy.cpp
    XSystemEntities.cpp
    XSystemServer.cpp
    XVehicle.cpp
    XVehicleProxy.cpp
    XVehicleProxyHeli.cpp
    XVehicleSystem.cpp
    XplayerVehicle.cpp
    INCLUDES
        "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS
        CryCommon
)
