// Mod 自有界面文案：覆盖游戏支持的八种文本语言；游戏地点名称不在此表翻译。
// 枚举与窗口/表格 ID 固定，切换语言不会重置导航；原生专名另由资源目录提供。
#pragma once
#include "localization.h"
#include <cstddef>

namespace tracker {
enum class UiText {
    SwitchMode,
    MapList,
    ShowHide,
    PauseResume,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    ModeCurrent,
    ModeInherited,
    InheritedColumn,
    ModeLabel,
    Paused,
    AreaInherited,
    AreaCurrent,
    OpenAreaMap,
    MarkerLegend,
    Title,
    ControllerLegend,
    ControllerHeld,
    Exploration,
    NameState,
    Unavailable,
    WaitingOn,
    WaitingOff,
    On,
    Off,
    RevealMap,
    UnvisitedTravel,
    TravelList,
    FeatureFailed,
    FeatureWaiting,
    FeatureNote,
    Filter,
    CloseList,
    PreviousPage,
    NextPage,
    Complete,
    PathColumn,
    MissingColumn,
    MapTitle,
    OnlyMissing,
    AllMaps,
    WaitingData,
    MapSummary,
    PageFormat,
    AllOpened,
    MapNote,
    RecordedLocation,
    TravelOrigin,
    TravelStoryOrigin,
    BuildingNote,
    ForestNote,
    OriginalPoint,
    HistoryCandidate,
    RecordFormat,
    RecordAuto,
    TravelUnavailable,
    TravelQueued,
    TravelClosing,
    TravelDispatched,
    WaitingScene,
    SceneUnsupported,
    OpenTravelMap,
    ReturnInvalid,
    ReturnStoryBlocked,
    ReturnMissing,
    RecordStorageFailed,
    ReturnFirst,
    StoryOwnsTravel,
    PositionNotReady,
    ConfirmReturn,
    ConfirmTravel,
    ArrivalUnconfirmed,
    RequestUnconfirmed,
    TravelReady,
    PreviousItem,
    NextItem,
    Confirm,
    CycleRecord,
    TravelNote,
    CurrentOpened,
    InheritedOpened,
    AreaNote,
    Count
};

// 字段顺序必须与 Language 枚举和 Localize 的八参数顺序一致。
struct UiTextEntry {
    const char* chinese; const char* japanese; const char* english; const char* traditionalChinese;
    const char* german; const char* french; const char* spanish; const char* korean;
};
// 每种语言保留相同的格式参数顺序与类型，由纯测试逐项校验；不得用英语占位冒充翻译。
inline constexpr UiTextEntry kUiTexts[] = {
    {"切换模式", "表示切替", "Switch mode", "切換模式", "Modus wechseln", "Changer de mode", "Cambiar modo", "모드 전환"}, // SwitchMode
    {"地图清单", "マップ一覧", "Map list", "地圖清單", "Kartenliste", "Liste des cartes", "Lista de mapas", "지도 목록"}, // MapList
    {"显示/隐藏", "表示/非表示", "Show/hide", "顯示/隱藏", "Anzeigen/ausblenden", "Afficher/masquer", "Mostrar/ocultar", "표시/숨기기"}, // ShowHide
    {"暂停/恢复", "停止/再開", "Pause/resume", "暫停/恢復", "Pausieren/fortsetzen", "Suspendre/reprendre", "Pausar/reanudar", "일시 정지/재개"}, // PauseResume
    {"View + 十字键上", "View + 方向キー上", "View + D-pad Up", "View + 十字鍵上", "View + Steuerkreuz oben", "View + Croix haut", "View + Cruceta arriba", "View + 십자키 위"}, // DpadUp
    {"View + 十字键下", "View + 方向キー下", "View + D-pad Down", "View + 十字鍵下", "View + Steuerkreuz unten", "View + Croix bas", "View + Cruceta abajo", "View + 십자키 아래"}, // DpadDown
    {"View + 十字键左", "View + 方向キー左", "View + D-pad Left", "View + 十字鍵左", "View + Steuerkreuz links", "View + Croix gauche", "View + Cruceta izq.", "View + 십자키 왼쪽"}, // DpadLeft
    {"View + 十字键右", "View + 方向キー右", "View + D-pad Right", "View + 十字鍵右", "View + Steuerkreuz rechts", "View + Croix droite", "View + Cruceta der.", "View + 십자키 오른쪽"}, // DpadRight
    {"本周目", "今周回", "Current run", "本輪遊玩", "Aktueller Durchlauf", "Partie actuelle", "Partida actual", "이번 회차"}, // ModeCurrent
    {"继承记录（多周目）", "引継ぎ記録（全周回）", "Carryover (all runs)", "繼承紀錄（多輪遊玩）", "Übernommen (alle Durchläufe)", "Données héritées (toutes parties)", "Registro heredado (todas las partidas)", "계승 기록 (모든 회차)"}, // ModeInherited
    {"继承记录", "引継ぎ記録", "Carryover", "繼承紀錄", "Übernommen", "Données héritées", "Registro heredado", "계승 기록"}, // InheritedColumn
    {"显示模式：%s", "表示モード：%s", "Display mode: %s", "顯示模式：%s", "Anzeigemodus: %s", "Mode d’affichage : %s", "Modo de visualización: %s", "표시 모드: %s"}, // ModeLabel
    {"宝箱标记已暂停（原版显示）", "宝箱マーカー停止中（通常表示）", "Chest markers paused (default display)", "寶箱標記已暫停（原版顯示）", "Truhenmarkierungen pausiert (Standardanzeige)", "Marqueurs de coffres suspendus (affichage normal)", "Marcadores de cofres en pausa (vista original)", "보물 상자 표시 일시 정지 (기본 표시)"}, // Paused
    {"继承记录当前地区已开  %u / %u", "現在エリア・引継ぎ開封済み  %u / %u", "Area opened, carryover  %u / %u", "繼承紀錄目前地區已開  %u / %u", "Gebiet, übernommen geöffnet  %u / %u", "Zone, ouverts hérités  %u / %u", "Zona, abiertos heredados  %u / %u", "현재 지역 계승 개봉 수  %u / %u"}, // AreaInherited
    {"本周目当前地区已开  %u / %u", "現在エリア・今周回開封済み  %u / %u", "Area opened, current run  %u / %u", "本輪遊玩目前地區已開  %u / %u", "Gebiet, im Durchlauf geöffnet  %u / %u", "Zone, ouverts dans cette partie  %u / %u", "Zona, abiertos en esta partida  %u / %u", "현재 지역 이번 회차 개봉 수  %u / %u"}, // AreaCurrent
    {"打开区域地图后显示两组地区统计", "地域マップで両方のエリア集計を表示", "Open the area map for both area counts", "開啟區域地圖後顯示兩組地區統計", "Gebietskarte für beide Gebietsstatistiken öffnen", "Ouvrez la carte de zone pour afficher les deux totaux", "Abre el mapa de zona para ver ambos recuentos", "지역 지도를 열면 두 가지 지역 통계를 표시합니다"}, // OpenAreaMap
    {"闭合箱标：未开    开启箱标：已开", "閉じた箱：未開封    開いた箱：開封済み", "Closed icon: unopened    Open icon: opened", "閉合箱標：未開    開啟箱標：已開", "Geschlossenes Symbol: ungeöffnet    Offenes Symbol: geöffnet", "Icône fermée : non ouvert    Icône ouverte : ouvert", "Icono cerrado: sin abrir    Icono abierto: abierto", "닫힌 아이콘: 미개봉    열린 아이콘: 개봉 완료"}, // MarkerLegend
    {"宝箱追踪  ·  0.6.0", "宝箱トラッカー  ·  0.6.0", "Chest Tracker  ·  0.6.0", "寶箱追蹤  ·  0.6.0", "Truhen-Tracker  ·  0.6.0", "Suivi des coffres  ·  0.6.0", "Seguimiento de cofres  ·  0.6.0", "보물 상자 추적  ·  0.6.0"}, // Title
    {"View：双窗口键；RS：按下右摇杆", "View：重なる四角のボタン／RS：右スティック押込", "View: two-window button; RS: click right stick", "View：雙視窗鍵；RS：按下右搖桿", "View: Taste mit zwei Fenstern; RS: rechten Stick drücken", "View : bouton à deux fenêtres ; RS : clic du stick droit", "View: botón de dos ventanas; RS: pulsar stick derecho", "View: 겹친 창 버튼 / RS: 오른쪽 스틱 누르기"}, // ControllerLegend
    {"View 已按住；RS：按下右摇杆", "View 押下中／RS：右スティック押込", "View held; RS: click right stick", "View 已按住；RS：按下右搖桿", "View gehalten; RS: rechten Stick drücken", "View maintenu ; RS : clic du stick droit", "View pulsado; RS: pulsar stick derecho", "View 누르는 중 / RS: 오른쪽 스틱 누르기"}, // ControllerHeld
    {"探索辅助", "探索補助", "Exploration aids", "探索輔助", "Erkundungshilfen", "Aides à l’exploration", "Ayudas de exploración", "탐색 보조"}, // Exploration
    {"%s：", "%s：", "%s: ", "%s：", "%s: ", "%s : ", "%s: ", "%s: "}, // NameState
    {"不可用", "利用不可", "Unavailable", "無法使用", "Nicht verfügbar", "Indisponible", "No disponible", "사용 불가"}, // Unavailable
    {"等待开启", "有効化待ち", "Enabling", "等待開啟", "Wird aktiviert", "Activation…", "Activando", "활성화 대기"}, // WaitingOn
    {"等待关闭", "無効化待ち", "Disabling", "等待關閉", "Wird deaktiviert", "Désactivation…", "Desactivando", "비활성화 대기"}, // WaitingOff
    {"已开启", "有効", "On", "已開啟", "Ein", "Activé", "Activado", "켜짐"}, // On
    {"已关闭", "無効", "Off", "已關閉", "Aus", "Désactivé", "Desactivado", "꺼짐"}, // Off
    {"地图全显", "マップ全表示", "Reveal map", "地圖全顯", "Karte aufdecken", "Révéler la carte", "Revelar mapa", "지도 전체 표시"}, // RevealMap
    {"未到访传送点", "未訪問の移動先", "Unvisited destinations", "未到訪傳送點", "Unbesuchte Reiseziele", "Destinations non visitées", "Destinos no visitados", "미방문 이동 지점"}, // UnvisitedTravel
    {"全传送清单", "全移動先一覧", "All destinations", "全傳送清單", "Alle Reiseziele", "Toutes les destinations", "Todos los destinos", "전체 이동 목록"}, // TravelList
    {"功能校验未通过，详见 tracker.log。", "機能の検証に失敗。tracker.log を確認してください。", "Feature check failed. See tracker.log.", "功能驗證未通過，詳見 tracker.log。", "Funktionsprüfung fehlgeschlagen. Siehe tracker.log.", "Échec du contrôle de la fonction. Consultez tracker.log.", "Error al verificar la función. Consulta tracker.log.", "기능 검증 실패. tracker.log를 확인해 주세요."}, // FeatureFailed
    {"等待刷新：打开地图或结束确认/转场。", "更新待ち：マップを開くか、確認・移動を終えてください。", "Awaiting refresh: open the map or finish the prompt/transition.", "等待更新：開啟地圖或結束確認/轉場。", "Aktualisierung ausstehend: Karte öffnen oder Dialog/Übergang beenden.", "Actualisation en attente : ouvrez la carte ou terminez le dialogue/la transition.", "Actualización pendiente: abre el mapa o termina el diálogo o la transición.", "갱신 대기: 지도를 열거나 확인 창/장면 전환을 마쳐 주세요."}, // FeatureWaiting
    {"重启关闭；传送可能越过入口剧情。", "再起動で無効化。移動は入口のイベントを飛ばす場合があります。", "Off after restart. Travel may skip entrance events.", "重啟後關閉；傳送可能略過入口劇情。", "Nach Neustart aus. Reisen kann Eingangsereignisse überspringen.", "Désactivé au redémarrage. Les voyages peuvent sauter des événements d’entrée.", "Se desactiva al reiniciar. Viajar puede omitir eventos de entrada.", "재시작 시 꺼짐. 이동으로 입구 이벤트를 건너뛸 수 있습니다."}, // FeatureNote
    {"全部/遗漏", "全件/未完了", "All/missing", "全部/遺漏", "Alle/fehlende", "Toutes/incomplètes", "Todos/pendientes", "전체/누락"}, // Filter
    {"收起清单", "一覧を閉じる", "Close list", "收起清單", "Liste schließen", "Fermer la liste", "Cerrar lista", "목록 닫기"}, // CloseList
    {"上一页", "前のページ", "Previous page", "上一頁", "Vorige Seite", "Page précédente", "Página anterior", "이전 페이지"}, // PreviousPage
    {"下一页", "次のページ", "Next page", "下一頁", "Nächste Seite", "Page suivante", "Página siguiente", "다음 페이지"}, // NextPage
    {"完成", "完了", "Complete", "完成", "Fertig", "Terminé", "Completo", "완료"}, // Complete
    {"大地图 / 地点", "地域 / 場所", "Region / location", "大地圖 / 地點", "Region / Ort", "Région / lieu", "Región / lugar", "지역 / 장소"}, // PathColumn
    {"未开", "未開封", "Unopened", "未開", "Ungeöffnet", "Non ouverts", "Sin abrir", "미개봉"}, // MissingColumn
    {"各地图宝箱收集", "マップ別の宝箱収集", "Chest progress by map", "各地圖寶箱收集", "Truhenfortschritt nach Karte", "Progression des coffres par carte", "Progreso de cofres por mapa", "지도별 보물 상자 수집"}, // MapTitle
    {"仅看有遗漏的地图", "未完了のマップのみ", "Maps with missing chests", "僅看有遺漏的地圖", "Nur Karten mit fehlenden Truhen", "Cartes avec des coffres manquants", "Mapas con cofres pendientes", "누락된 상자가 있는 지도만"}, // OnlyMissing
    {"全部地图（有遗漏的在前）", "全マップ（未完了を先に表示）", "All maps (incomplete first)", "全部地圖（有遺漏的在前）", "Alle Karten (unvollständige zuerst)", "Toutes les cartes (incomplètes en premier)", "Todos los mapas (incompletos primero)", "전체 지도 (미완료 우선)"}, // AllMaps
    {"等待游戏数据……", "ゲームデータを待機中…", "Waiting for game data...", "等待遊戲資料……", "Warte auf Spieldaten…", "En attente des données du jeu…", "Esperando datos del juego…", "게임 데이터 대기 중…"}, // WaitingData
    {"已完成 %u / %u 张地图    剩余未开 %u 个", "完了マップ %u / %u    未開封 %u 個", "Maps complete: %u / %u    Unopened: %u", "已完成 %u / %u 張地圖    剩餘未開 %u 個", "Karten fertig: %u / %u    Ungeöffnet: %u", "Cartes terminées : %u / %u    Non ouverts : %u", "Mapas completos: %u / %u    Sin abrir: %u", "완료 지도 %u / %u    미개봉 %u개"}, // MapSummary
    {"第 %zu / %zu 页", "%zu / %zu ページ", "Page %zu / %zu", "第 %zu / %zu 頁", "Seite %zu / %zu", "Page %zu / %zu", "Página %zu / %zu", "%zu / %zu 페이지"}, // PageFormat
    {"当前显示模式下，所有宝箱均已开。", "現在の表示モードでは、すべての宝箱が開封済みです。", "All chests are opened in this display mode.", "目前顯示模式下，所有寶箱均已開。", "In diesem Anzeigemodus sind alle Truhen geöffnet.", "Tous les coffres sont ouverts dans ce mode d’affichage.", "Todos los cofres están abiertos en este modo.", "현재 표시 모드에서는 모든 보물 상자가 개봉되어 있습니다."}, // AllOpened
    {"计数：已开 / 总数；道路单列，迷宫含各楼层，包含未到达地点。", "数値は開封済み / 総数。街道は別集計、ダンジョンは全階層を合算。未到達の場所も含みます。", "Opened / total. Roads are separate; dungeons include all floors. Unreached locations are included.", "計數：已開 / 總數；道路分開列出，迷宮含各樓層，包含未到達地點。", "Geöffnet / gesamt. Straßen separat; Dungeons umfassen alle Etagen. Noch nicht erreichte Orte sind enthalten.", "Ouverts / total. Routes séparées ; donjons sur tous les étages. Les lieux non atteints sont inclus.", "Abiertos / total. Caminos por separado; mazmorras con todas sus plantas. Incluye lugares no alcanzados.", "개봉 수 / 총수. 도로는 별도 집계, 던전은 전 층 합산. 미도달 장소도 포함합니다."}, // MapNote
    {"记录地点", "記録した場所", "Recorded location", "記錄地點", "Aufgezeichneter Ort", "Lieu enregistré", "Lugar registrado", "기록된 장소"}, // RecordedLocation
    {"传送保留最初出发点。", "最初の出発地点を保持します。", "Your original departure point is kept.", "傳送保留最初出發點。", "Der ursprüngliche Ausgangspunkt bleibt erhalten.", "Le point de départ initial est conservé.", "Se conserva el punto de partida inicial.", "최초 출발 지점을 유지합니다."}, // TravelOrigin
    {"按当前剧情开放；传送保留最初出发点。", "進行状況に応じて開放。最初の出発地点を保持します。", "Available by story progress; original departure point is kept.", "依目前劇情開放；傳送保留最初出發點。", "Nach Storyfortschritt verfügbar; ursprünglicher Ausgangspunkt bleibt erhalten.", "Selon la progression ; le point de départ initial est conservé.", "Según el progreso de la historia; se conserva el punto de partida inicial.", "진행 상황에 따라 개방하며 최초 출발 지점을 유지합니다."}, // TravelStoryOrigin
    {"传送后可步行进入相邻建筑，补开遗漏宝箱。", "移動後は隣接する建物に歩いて入り、未開封の宝箱を回収できます。", "After travel, enter the nearby building on foot to collect missed chests.", "傳送後可步行進入相鄰建築，開啟遺漏寶箱。", "Nach der Reise kannst du das benachbarte Gebäude zu Fuß betreten und fehlende Truhen öffnen.", "Après le voyage, entrez à pied dans le bâtiment voisin pour récupérer les coffres manqués.", "Tras viajar, entra a pie en el edificio cercano para recoger los cofres pendientes.", "이동 후 인접한 건물에 걸어 들어가 누락된 보물 상자를 열 수 있습니다."}, // BuildingNote
    {"所选地点没有普通出口；请使用记录的出发点返程。", "選択した場所には通常の出口がありません。記録した出発地点へ戻ってください。", "This location has no normal exit. Return using your recorded departure point.", "所選地點沒有一般出口；請使用記錄的出發點返程。", "Dieser Ort hat keinen normalen Ausgang. Kehre über den aufgezeichneten Ausgangspunkt zurück.", "Ce lieu n’a pas de sortie normale. Revenez au point de départ enregistré.", "Este lugar no tiene salida normal. Regresa al punto de partida registrado.", "이 장소에는 일반 출구가 없습니다. 기록된 출발 지점으로 돌아가 주세요."}, // ForestNote
    {"最初出发点", "最初の出発地点", "Original departure", "最初出發點", "Ursprünglicher Ausgangspunkt", "Départ initial", "Partida inicial", "최초 출발 지점"}, // OriginalPoint
    {"历史返程候选", "過去の帰還候補", "Return history candidate", "歷史返程候選", "Früheres Rückkehrziel", "Retour historique proposé", "Destino de regreso del historial", "이전 귀환 후보"}, // HistoryCandidate
    {"%s  ·  %s  ·  记录 %zu / %zu", "%s  ·  %s  ·  記録 %zu / %zu", "%s  ·  %s  ·  Record %zu / %zu", "%s  ·  %s  ·  紀錄 %zu / %zu", "%s  ·  %s  ·  Eintrag %zu / %zu", "%s  ·  %s  ·  Entrée %zu / %zu", "%s  ·  %s  ·  Registro %zu / %zu", "%s  ·  %s  ·  기록 %zu / %zu"}, // RecordFormat
    {"首次出发前自动记录场景、站立坐标与朝向。", "初回の移動前に、場所・立ち位置・向きを自動記録します。", "The first departure records your scene, position and facing automatically.", "首次出發前自動記錄場景、站立座標與朝向。", "Vor der ersten Reise werden Szene, Position und Blickrichtung automatisch gespeichert.", "La scène, la position et l’orientation sont enregistrées avant le premier départ.", "Antes del primer viaje se registran la escena, la posición y la orientación.", "첫 출발 전에 장면, 위치, 방향을 자동으로 기록합니다."}, // RecordAuto
    {"回访保护或原生入口校验未通过，暂不可用。", "再訪保護またはゲーム側入口の検証に失敗したため、利用できません。", "Travel safeguards or native entry checks failed; unavailable.", "回訪保護或原生入口驗證未通過，暫時無法使用。", "Reiseschutz oder Prüfung des Spieleinstiegs fehlgeschlagen; derzeit nicht verfügbar.", "Échec des protections de voyage ou du contrôle de l’entrée native ; indisponible.", "Fallaron las protecciones de viaje o la verificación de la entrada nativa; no disponible.", "재방문 보호 또는 게임 진입점 검증 실패로 사용할 수 없습니다."}, // TravelUnavailable
    {"请求已提交，等待原生地图线程核对……", "要求送信済み。ゲーム側のマップ処理で確認中…", "Request queued; waiting for the game map thread...", "請求已送出，等待原生地圖執行緒核對……", "Anfrage eingereiht; warte auf Prüfung durch die Kartenverarbeitung…", "Demande envoyée ; vérification par le traitement de carte du jeu…", "Solicitud enviada; esperando la comprobación del mapa del juego…", "요청 제출 완료. 게임 지도 처리 확인 대기 중…"}, // TravelQueued
    {"原生地图正在关闭，等待换图……", "ゲームのマップを閉じています。移動を待機中…", "Closing the game map; waiting for the scene change...", "原生地圖正在關閉，等待換圖……", "Spielkarte wird geschlossen; warte auf Szenenwechsel…", "Fermeture de la carte ; en attente du changement de scène…", "Cerrando el mapa del juego; esperando el cambio de escena…", "게임 지도를 닫는 중. 장면 전환 대기 중…"}, // TravelClosing
    {"正在换图，等待实际到达确认……", "移動中。実際の到着を確認しています…", "Changing scenes; waiting for arrival confirmation...", "正在換圖，等待實際到達確認……", "Szenenwechsel läuft; warte auf Ankunftsbestätigung…", "Changement de scène ; vérification de l’arrivée…", "Cambiando de escena; esperando la confirmación de llegada…", "장면 전환 중. 실제 도착 확인 대기 중…"}, // TravelDispatched
    {"等待游戏场景数据……", "シーンデータを待機中…", "Waiting for scene data...", "等待遊戲場景資料……", "Warte auf Szenendaten…", "En attente des données de scène…", "Esperando datos de la escena…", "게임 장면 데이터 대기 중…"}, // WaitingScene
    {"当前场景或章节数据尚未支持。", "現在のシーンまたは章のデータには未対応です。", "The current scene or chapter data is not supported.", "目前場景或章節資料尚未支援。", "Die aktuelle Szene oder die Kapiteldaten werden noch nicht unterstützt.", "La scène ou les données du chapitre actuel ne sont pas prises en charge.", "La escena o los datos del capítulo actual aún no son compatibles.", "현재 장면 또는 장 데이터는 아직 지원하지 않습니다."}, // SceneUnsupported
    {"请打开游戏地图，退出子窗口并等待地图动画结束。", "ゲームのマップを開き、子ウィンドウを閉じてアニメーションの終了を待ってください。", "Open the game map, close subwindows and wait for its animation to finish.", "請開啟遊戲地圖，離開子視窗並等待地圖動畫結束。", "Spielkarte öffnen, Unterfenster schließen und das Ende der Kartenanimation abwarten.", "Ouvrez la carte, fermez les sous-fenêtres et attendez la fin de l’animation.", "Abre el mapa del juego, cierra las subventanas y espera a que termine la animación.", "게임 지도를 열고 하위 창을 닫은 뒤 지도 애니메이션이 끝날 때까지 기다려 주세요."}, // OpenTravelMap
    {"返程地点数据尚未通过校验，请核对所选记录。", "帰還先データの検証に失敗しました。選択した記録を確認してください。", "The return location has not passed validation. Check the selected record.", "返程地點資料尚未通過驗證，請核對所選紀錄。", "Rückkehrziel nicht validiert. Bitte den gewählten Eintrag prüfen.", "Le lieu de retour n’a pas passé la validation. Vérifiez l’entrée sélectionnée.", "No se ha validado el destino de regreso. Revisa el registro seleccionado.", "귀환 지점 데이터 검증에 실패했습니다. 선택한 기록을 확인해 주세요."}, // ReturnInvalid
    {"当前剧情暂不允许返回此地点，请先完成游戏原生传送剧情。", "現在の進行状況では帰還できません。まず通常の移動イベントを進めてください。", "Story progress prevents returning here. Complete the native travel event first.", "目前劇情暫不允許返回此地點，請先完成遊戲原生傳送劇情。", "Die Story erlaubt hier noch keine Rückkehr. Zuerst das normale Reiseereignis abschließen.", "La progression empêche ce retour. Terminez d’abord l’événement de voyage normal.", "La historia impide volver aquí. Completa primero el evento de viaje normal.", "현재 진행 상황에서는 돌아갈 수 없습니다. 먼저 게임의 일반 이동 이벤트를 완료해 주세요."}, // ReturnStoryBlocked
    {"没有本章节的返程记录；请读取正常地区存档后出发。", "この章の帰還記録がありません。通常の地域のセーブデータをロードして出発してください。", "No return record for this chapter. Load a save in a normal area before departing.", "沒有本章節的返程紀錄；請讀取一般地區存檔後出發。", "Kein Rückkehreintrag für dieses Kapitel. Vor der Reise einen Spielstand in einem normalen Gebiet laden.", "Aucun retour pour ce chapitre. Chargez une sauvegarde dans une zone normale avant de partir.", "No hay registro de regreso para este capítulo. Carga una partida en una zona normal antes de salir.", "이번 장의 귀환 기록이 없습니다. 일반 지역의 저장 데이터를 불러온 뒤 출발해 주세요."}, // ReturnMissing
    {"返程记录无法安全保存，本次出发已阻止。", "帰還記録を安全に保存できないため、移動を中止しました。", "Travel blocked: the return record cannot be saved safely.", "返程紀錄無法安全儲存，已阻止本次出發。", "Reise blockiert: Der Rückkehreintrag lässt sich nicht sicher speichern.", "Voyage bloqué : l’enregistrement du retour ne peut pas être sauvegardé de façon sûre.", "Viaje bloqueado: no se puede guardar el registro de regreso de forma segura.", "귀환 기록을 안전하게 저장할 수 없어 이동을 차단했습니다."}, // RecordStorageFailed
    {"重启或读档后，请先选择历史返程点返回，再开始新回访。", "再起動・ロード後は、過去の帰還先へ戻ってから新たな再訪を始めてください。", "After restart or loading, return using a history record before starting a new trip.", "重啟或讀檔後，請先選擇歷史返程點返回，再開始新回訪。", "Nach Neustart oder Laden zuerst über einen früheren Eintrag zurückkehren, dann eine neue Reise beginnen.", "Après un redémarrage ou un chargement, utilisez un retour historique avant de commencer un nouveau voyage.", "Tras reiniciar o cargar, regresa mediante el historial antes de iniciar un nuevo viaje.", "재시작하거나 불러온 뒤에는 이전 귀환 기록으로 돌아간 후 새 재방문을 시작해 주세요."}, // ReturnFirst
    {"当前传送由剧情接管，请先使用游戏原生传送继续剧情。", "現在の移動はイベントが制御しています。通常の移動でストーリーを進めてください。", "A story event controls travel. Use native travel to continue the story first.", "目前傳送由劇情接管，請先使用遊戲原生傳送繼續劇情。", "Ein Storyereignis steuert die Reise. Zuerst mit der normalen Reisefunktion die Story fortsetzen.", "Un événement contrôle le voyage. Utilisez le déplacement normal pour poursuivre l’histoire.", "Un evento de la historia controla el viaje. Usa el desplazamiento normal para continuar.", "현재 이동은 스토리 이벤트가 제어합니다. 먼저 게임의 일반 이동으로 스토리를 진행해 주세요."}, // StoryOwnsTravel
    {"当前站位尚无法安全记录，暂不能传送。", "現在の立ち位置を安全に記録できないため、まだ移動できません。", "Your current position cannot be recorded safely yet; travel is unavailable.", "目前站立位置尚無法安全記錄，暫時無法傳送。", "Die aktuelle Position lässt sich noch nicht sicher speichern; Reise nicht verfügbar.", "La position actuelle ne peut pas encore être enregistrée de façon sûre ; voyage indisponible.", "Aún no se puede registrar tu posición de forma segura; el viaje no está disponible.", "현재 위치를 아직 안전하게 기록할 수 없어 이동할 수 없습니다."}, // PositionNotReady
    {"核对上方地点和时间，再按一次确认返程；记录不绑定存档槽位。", "上の場所と時刻を確認し、もう一度決定すると帰還します。記録はセーブ枠に紐づきません。", "Check the location and time above, then confirm again to return. Records are not tied to save slots.", "核對上方地點和時間，再按一次確認返程；紀錄不綁定存檔欄位。", "Ort und Zeit oben prüfen, dann erneut zum Zurückkehren bestätigen. Einträge sind nicht an Speicherplätze gebunden.", "Vérifiez le lieu et l’heure ci-dessus, puis confirmez à nouveau. Les entrées ne sont pas liées à un emplacement de sauvegarde.", "Revisa el lugar y la hora de arriba y confirma de nuevo para regresar. Los registros no están vinculados a ranuras de guardado.", "위의 장소와 시간을 확인한 뒤 다시 확인하면 귀환합니다. 기록은 저장 슬롯에 연결되지 않습니다."}, // ConfirmReturn
    {"再次按确认组合键前往所选地点（8 秒内有效）。", "8 秒以内にもう一度決定の組合せキーを押すと移動します。", "Press the confirm shortcut again within 8 seconds to travel.", "再次按下確認組合鍵前往所選地點（8 秒內有效）。", "Innerhalb von 8 Sekunden erneut die Bestätigungskombination drücken, um zu reisen.", "Appuyez à nouveau sur le raccourci de confirmation dans les 8 secondes pour voyager.", "Pulsa de nuevo el atajo de confirmación en 8 segundos para viajar.", "8초 안에 확인 단축키를 다시 누르면 선택한 장소로 이동합니다."}, // ConfirmTravel
    {"上次到达未能自动确认，出发点已保留；现在可重新选择传送或返程。", "前回の到着を自動確認できませんでした。出発地点は保持され、移動・帰還を再選択できます。", "Arrival was not confirmed automatically. Departure point kept; you may travel or return again.", "上次到達未能自動確認，出發點已保留；現在可重新選擇傳送或返程。", "Ankunft nicht automatisch bestätigt. Ausgangspunkt behalten; erneute Reise oder Rückkehr ist möglich.", "Arrivée non confirmée automatiquement. Départ conservé ; vous pouvez voyager ou revenir à nouveau.", "No se confirmó automáticamente la llegada. Se conserva el punto de partida; puedes viajar o regresar otra vez.", "지난 도착을 자동으로 확인하지 못했습니다. 출발 지점은 유지되며 다시 이동하거나 귀환할 수 있습니다."}, // ArrivalUnconfirmed
    {"上次请求未确认成功；原返程记录保留，请重新打开地图核对。", "前回の要求の成功を確認できませんでした。帰還記録は保持されています。マップを開き直してください。", "The last request was not confirmed. Return record kept; reopen the game map to check.", "上次請求未確認成功；原返程紀錄保留，請重新開啟地圖核對。", "Letzte Anfrage nicht bestätigt. Rückkehreintrag behalten; Spielkarte zum Prüfen erneut öffnen.", "Dernière demande non confirmée. Retour conservé ; rouvrez la carte pour vérifier.", "No se confirmó la última solicitud. Se conserva el registro de regreso; vuelve a abrir el mapa para comprobarlo.", "지난 요청의 성공을 확인하지 못했습니다. 귀환 기록은 유지됩니다. 지도를 다시 열어 확인해 주세요."}, // RequestUnconfirmed
    {"就绪，请选择目的地。", "準備完了。移動先を選択してください。", "Ready. Select a destination.", "就緒，請選擇目的地。", "Bereit. Bitte ein Reiseziel wählen.", "Prêt. Choisissez une destination.", "Listo. Elige un destino.", "준비 완료. 목적지를 선택해 주세요."}, // TravelReady
    {"上一项", "前の項目", "Previous item", "上一項", "Voriger Eintrag", "Entrée précédente", "Elemento anterior", "이전 항목"}, // PreviousItem
    {"下一项", "次の項目", "Next item", "下一項", "Nächster Eintrag", "Entrée suivante", "Elemento siguiente", "다음 항목"}, // NextItem
    {"确认", "決定", "Confirm", "確認", "Bestätigen", "Confirmer", "Confirmar", "확인"}, // Confirm
    {"切换返程记录", "帰還記録を切替", "Cycle return record", "切換返程紀錄", "Rückkehreintrag wechseln", "Changer de retour", "Cambiar regreso", "귀환 기록 전환"}, // CycleRecord
    {"打开游戏地图，选好地点后确认两次；末页可返程。", "ゲームのマップを開き、移動先を選んで 2 回決定。最後のページから帰還できます。", "Open the game map, select a destination and confirm twice. Return is on the last page.", "開啟遊戲地圖，選好地點後確認兩次；末頁可返程。", "Spielkarte öffnen, Ziel wählen und zweimal bestätigen. Rückkehr auf der letzten Seite.", "Ouvrez la carte, choisissez un lieu et confirmez deux fois. Le retour est à la dernière page.", "Abre el mapa, elige un destino y confirma dos veces. El regreso está en la última página.", "게임 지도를 열고 장소를 선택한 뒤 두 번 확인하세요. 마지막 페이지에서 귀환할 수 있습니다."}, // TravelNote
    {"本周目已开  %u / 566", "今周回の開封済み  %u / 566", "Opened, current run  %u / 566", "本輪遊玩已開  %u / 566", "Im Durchlauf geöffnet  %u / 566", "Ouverts, partie actuelle  %u / 566", "Abiertos, partida actual  %u / 566", "이번 회차 개봉 수  %u / 566"}, // CurrentOpened
    {"继承记录已开  %u / 566", "引継ぎの開封済み  %u / 566", "Opened, carryover  %u / 566", "繼承紀錄已開  %u / 566", "Übernommen geöffnet  %u / 566", "Ouverts, données héritées  %u / 566", "Abiertos, registro heredado  %u / 566", "계승 기록 개봉 수  %u / 566"}, // InheritedOpened
    {"地区统计包含相邻道路", "エリア集計には隣接する街道を含みます", "Area counts include adjacent roads", "地區統計包含相鄰道路", "Gebietsstatistiken enthalten benachbarte Straßen", "Les totaux de zone incluent les routes voisines", "El recuento de zona incluye caminos adyacentes", "지역 통계에는 인접 도로가 포함됩니다"}, // AreaNote
};
static_assert(sizeof(kUiTexts) / sizeof(kUiTexts[0]) == static_cast<size_t>(UiText::Count));

inline const char* UiString(UiText id) noexcept {
    const auto& text = kUiTexts[static_cast<size_t>(id)];
    return Localize(text.chinese, text.japanese, text.english, text.traditionalChinese,
                    text.german, text.french, text.spanish, text.korean);
}
}
