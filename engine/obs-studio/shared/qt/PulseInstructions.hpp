#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QGuiApplication>
#include <QScreen>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace PulseInstructions {

inline QWidget *createPage(QWidget *parent)
{
	auto *page = new QWidget(parent);
	page->setObjectName("PulseWeaverInstructionsPage");
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(18, 16, 18, 16);
	auto *title = new QLabel("Lumia Stream setup", page);
	title->setStyleSheet("font-size:24px;font-weight:700;");
	layout->addWidget(title);
	auto *intro = new QLabel("Set up your Lumia overlays and the actions that make them work. Open this guide from the Lumia logo beside the readiness status on Show Control.", page);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *topics = new QTabWidget(page);
	topics->setObjectName("PulseWeaverInstructionTopics");
	topics->setAccessibleName("Lumia setup topics");
	layout->addWidget(topics, 1);
	auto topic = [topics](const QString &name, const QString &content) {
		auto *browser = new QTextBrowser(topics);
		browser->setAccessibleName(name + " instructions");
		browser->setOpenExternalLinks(true);
		browser->setHtml("<html><body style='font-size:13pt;'>" + content + "</body></html>");
		topics->addTab(browser, name);
	};
	topic("Lumia overlays", QStringLiteral(R"PW(
<h2>Import and connect your overlays</h2>
<p>A <b>Stage</b> is a part of your show, such as Starting or Game. A <b>Look</b> is a layout you switch to within that Stage. A <b>source</b> supplies a camera, capture, video or overlay. Each Look has landscape and portrait versions.</p>
<p><b>Lumia Stream is required for the Lumia overlay templates for Pulse Weaver, including the countdown and widgets.</b> Install Lumia, connect its Pulse Weaver plugin and import the templates. On page 4, reuse their existing Lumia browser sources or add their browser links. The plugin is also the recommended way to control animations, Stage / Look changes and show actions.</p>
<ol><li><a href="https://www.lumiastream.com/downloads">Install Lumia Stream</a>. Download the <a href="https://github.com/darylbwickham-design/pulse-weaver-studio/releases/latest/download/PulseWeaver-Lumia-1.4.4.lumiaplugin">Pulse Weaver companion plugin</a>, import its <code>.lumiaplugin</code> file into Lumia, and follow its connection instructions for the Pulse Weaver installation you are using.</li>
<li>Import the templates needed by your chosen Looks into Lumia. H is landscape and V is portrait. Customize the text, logo, colours and artwork in Lumia.</li>
<li>Use each imported overlay's chain / copy-link button to get your own browser link. On page 4, assign MAIN to Countdown, READY to Getting ready, BRB to Be right back and END to Ending, using each Look's separate landscape and portrait artwork questions. A template share link imports a template; it is not the browser link.</li>
<li>Add any accompanying commands once. Both canvas versions can use the same command set. Select your own Twitch rewards and Pulse Weaver Stages / Looks after importing.</li></ol>
<p>Click stage and Look cards to plan your show. Recommended layouts are ready to use; open <b>Layout options…</b> when you want to adjust a Look. On page 4, click <b>Use existing…</b>, <b>Create new…</b> or <b>Paste Lumia link</b>, then <b>Next source</b>. Use <b>Skip this optional source</b> for an unused feed or layer. A source can be reused across stages; Feed A in one stage does not force Feed A in another stage to use the same input. New sources are created when you choose <b>Create my show</b>.</p>
<p>The source status tells you whether the current question is required or optional. <b>Next source needing attention</b> jumps past completed and empty optional questions; <b>Review source assignments…</b> opens clickable choices for returning to any question. The placement diagram follows the selected stage tab, Look or source question. Landscape artwork drafted in the wizard stays in landscape artwork choices; create its matching portrait version separately.</p>
<p>Use the shared alert overlay for alerts and the Fortnite widget. There are <b>three shared alert / overlay layers for each canvas</b>: layer 1 is lowest and layer 3 is highest. Choose an existing source or add a browser link, then show it in all selected Looks, one stage group or one Look. These layers also work with the saved layouts you choose to copy. Leave unused layers empty. Add optional chat, camera frames and Newsroom only when your chosen Looks use them.</p>
<h3>Lumia overlay templates for Pulse Weaver</h3>
<p>These Lumia share links import the matching template into your own library. Choose the pairs your Looks use:</p>
<table cellspacing="8"><tr><th align="left">Template pair</th><th>Landscape</th><th>Portrait</th></tr><tr><td>Starting countdown</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b4f-50b6-4733-a26c-e8610557564d">PW-MAIN01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b68-98a4-426a-b4d3-9b073f539d90">PW-MAIN01-V</a></td></tr><tr><td>Getting ready</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6a-264d-442f-a79d-59cd0be76fa7">PW-READY01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6b-f146-4b08-82a4-f31284fbf23a">PW-READY01-V</a></td></tr><tr><td>Be right back</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6d-3094-478b-8fb7-a8016dfc9cbe">PW-BRB01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6e-c0bd-4dcf-a9bb-a7f036cc58cf">PW-BRB01-V</a></td></tr><tr><td>Ending + Credits</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b76-7ac2-4f2a-96bb-1c150ef6c7f8">PW-END01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b7c-a5a9-4be1-8aa6-d83f9f10f94c">PW-END01-V</a></td></tr><tr><td>Shared alerts + Fortnite</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b7e-f534-4553-add7-cd2644a64489">PW-ALERT01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38ba7-de88-4b45-a79c-068c63182298">PW-ALERT01-V</a></td></tr><tr><td>Newsroom / news intro</td><td><a href="https://api.lumiastream.com/shared/download/a2f38ba9-478c-4d1b-880a-d1953294ef2a">PW-NEWS01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38baa-cc3f-4f42-b629-6e26b50d60bc">PW-NEWS01-V</a></td></tr></table>
<p>When importing <b>PW-ALERT01-H</b>, accept its accompanying command bundle once. The portrait copy has no duplicate commands. See Fortnite challenges for the reward setup.</p>
<h3>Newsroom and news intro</h3>
<p>For Newsroom, add NEWS to a shared overlay layer on page 4 and choose the specific Look that should show it. Add the matching H/V copies to their respective canvas slots. Use a group or all Looks only when you want the overlay throughout those presentations. The shared ALERT overlay can target all selected Looks or a stage group.</p>
<p>Newsroom is visible on load by default. For an event-only News intro, import a separate copy in Lumia and turn off <b>Show persistent Newsroom on load</b>. Its Code ID is <code>PW-NEWS01</code>. A news payload plays the intro and bulletin; a stop payload clears it:</p>
<pre>{"type":"news","title":"Your bulletin","message":"Your update"}
{"type":"stop"}</pre>
<p>Customize the logo, sound, desk, title and ticker in Lumia. New browser sources start with their audio excluded from output; existing source audio settings are kept. After creation, enable sound for one intended source in audio routing and keep its other canvas copy silent, so alerts, HUDFX and TTS have one audible owner. Optional chat and camera frames use your own sources; they are not additional templates in this list.</p>
<p>The wizard shows placement diagrams. With streaming and recording stopped, check the actual landscape and portrait output in Control after creation, then test your timer, alerts and transitions.</p>
<p><a href="https://lumiastream.com/services/overlays">About Lumia overlays</a> &nbsp; | &nbsp;
<a href="https://forum.lumiastream.com/t/lumia-guides-photo-time/1285">Lumia's command import guide</a></p>
)PW"));
	topic("Starting countdown", QStringLiteral(R"PW(
<h2>Show Starting and start the timer</h2>
<p>The Starting countdown runs in the imported Lumia artwork. Its duration comes from the start payload below. Assign the matching MAIN landscape and portrait browser sources in the wizard.</p>
<p>Choosing the Starting presentation and starting its Lumia countdown are two actions. The timer payload does not change your Stage.</p>
<ol><li>Create a command or shortcut in Lumia. Add Pulse Weaver's <b>Run Stage Look / Motion Action</b> and choose your show's Starting Countdown Look. For the saved layout preset, choose its Starting Look and select the Countdown content. If your plugin offers <b>Activate Stage</b>, choose Starting and separately select its countdown presentation.</li>
<li>Ensure the Starting overlays are loaded before the timer action. Check this with your chosen stinger; add a short delay if loading requires it.</li>
<li>Add <b>Send Custom Overlay Content</b>. Set <b>Code ID</b> to <code>start-screen</code> and send:<br><pre>{"action":"start","durationSeconds":600}</pre>
This starts ten minutes. Change 600 to your desired duration in seconds.</li></ol>
<p>Send the start action once to the shared route for both formats. Keep overlay autostart off for this setup. Choose one timer-start trigger; multiple platform-start alerts can restart the timer.</p>
<h3>Stop or restart</h3>
<p>To stop the countdown, send this to <code>start-screen</code>:</p><pre>{"action":"stop"}</pre>
<p>Stopping the timer does not change Stage. Sending start again restarts the full duration.</p>
<h3>When the countdown finishes</h3>
<ol><li>Create a Lumia command, for example <code>countdown-finished</code>. Add Pulse Weaver's <b>Run Stage Look / Motion Action</b> and select the Stage / Look you want to show next.</li>
<li>In <b>one</b> Starting overlay's Lumia settings, enter that exact command name in <b>Your countdown completion command</b> (<code>goLiveCommand</code>).</li>
<li>Enable <b>Run completion command (enable on ONE canvas only)</b> (<code>autoFireGoLive</code>) on that instance. When the timer reaches zero, it calls the named Lumia command.</li>
<li>Keep completion disabled in the other orientation and every additional timer copy. Shared templates begin with this disabled and the command name blank.</li></ol>
<p><b>Use exactly one completion-command owner across all timer instances.</b> This is separate from choosing one audible source for overlay sounds. With streaming and recording stopped, select Starting, send a short countdown and check that your command runs once and opens the intended next Look. Reopening or refreshing a timer browser during countdown needs a restart check.</p>
<p>Starting the broadcast is a separate optional Start Show / Start Platform action. Configure your destinations before adding it.</p>
<p>These settings and payloads are part of the supplied Starting templates. Create the Lumia command with your own generated Stage / Look selected; it is not imported as a ready-bound command.</p>
)PW"));
	topic("Chat Physics", QStringLiteral(R"PW(
<h2>Add Chat Box Physics to your show</h2>
<p><b>Chat Box Physics</b> by <b>TangoTiddies</b> is available in the Lumia plugin marketplace. Its installed settings and actions may use the internal name <b>Chat Physics Local</b>. It receives chat and alert events from Lumia and supplies independent landscape and portrait browser outputs.</p>
<ol><li>Open <a href="https://lumiastream.com/plugins#chat_physics_local">Chat Box Physics in the Lumia plugin marketplace</a>, confirm the creator is <b>TangoTiddies</b>, and choose <b>Install Plugin on Lumia Stream</b>. Enable the plugin in Lumia.</li>
<li>In <b>Lumia Settings → API</b>, enable the Developers API. Put its token in the plugin's <b>Connection → Lumia Developers API token</b> setting. Keep the host at <code>127.0.0.1</code> and use the API port shown by Lumia, normally <code>39231</code>. The token stays in the plugin; it does not belong in a Pulse Weaver browser link.</li>
<li>On wizard page 4, the suggested Chat Physics slot is <b>shared overlay layer 2</b> in both formats. Click <b>Use default Chat Box Physics link</b> to fill the matching link: <code>http://127.0.0.1:39270/</code> for landscape, <code>http://127.0.0.1:39270/portrait</code> for portrait. Choose the Looks that should show chat. You can use another free layer. If you changed <b>Local overlay port</b>, enter that port in both links.</li>
<li>Use separate browser sources: landscape <b>1920 × 1080</b>, portrait <b>1080 × 1920</b>. These are the source viewports; fit each source to its destination canvas. Keep <b>Shutdown source when not visible</b> and <b>Refresh browser when scene becomes active</b> off. The wizard leaves newly added browser sources loaded between Looks.</li>
<li>Connect your chat services inside Lumia. Use <b>Preview Chat Physics</b> in Lumia's action picker to check the local output, then verify a message from your connected chat before going live.</li></ol>
<p>Both outputs share one Lumia connection. Do not create a second plugin instance or copy the landscape browser source into portrait.</p>
<h3>Coordinate chat with camera layouts</h3>
<ol><li>Create a Lumia command for the Look you want to select. Add <b>Change Chat Camera Clearances</b> and choose its <b>Output</b>. Set <b>Top clearance</b>, <b>Bottom clearance</b>, <b>Side inset</b> and <b>Gap between cards</b> in that source's pixels to leave room for the camera.</li>
<li>Add <b>Change Chat Physics Mode</b>. Choose <b>Upright chat box</b> or <b>Compact single line</b> when using these clearances, then select the chat placement. Set <b>Revert after seconds</b> to <b>0</b> to keep this layout. Enable <b>Clear existing cards first</b> when all messages should use the newly chosen mode.</li>
<li>In the same command, add Pulse Weaver's <b>Run Stage Look / Motion Action</b> and select the matching Stage / Look. Use separate Landscape and Portrait actions when their camera positions differ; choose Both outputs only when the same chat settings fit both.</li></ol>
<p>Clearances reflow existing upright cards. They do not constrain Physics stack, Scatter stack, Centre burst, Ambient float or emote-wall effects; configure their physics placement area separately. Chat does not automatically track moving cameras, so choose space that stays clear throughout the movement.</p>
<p><b>Revert after seconds is a hold time before restoring the earlier layout, not a movement duration.</b> Existing cards retain their original mode unless cleared. <b>Restore Saved Chat Layout</b> restores the selected output's saved layout and cancels temporary layout changes.</p>
<h3>Alerts and troubleshooting</h3>
<p>If you add <b>Render Alert in Chat Physics</b> to your own alert action chains, disable <b>Automatically render enabled Lumia alerts</b> when those chains already handle every alert, to avoid duplicate cards. Select one audible owner if other overlays also play that event's sound.</p>
<p>If chat is empty, check that the message reaches Lumia and the plugin is enabled, then use <b>Reconnect Local Chat Feed</b>. Refreshing a browser clears that browser's existing cards. The local plugin must remain running for its browser links to work.</p>
)PW"));
	topic("Fortnite challenges", QStringLiteral(R"PW(
<h2>Commands for the shared alert widget</h2>
<p>Install the command set once for the landscape and portrait alerts. Create your own Twitch Points rewards and choose their costs and enabled state. The import contains disabled code helpers, not ready-made reward bindings:</p>
<table cellspacing="10"><tr><th align="left">Reward purpose</th><th align="left">Code helper</th></tr>
<tr><td>Random challenge</td><td>pwfnrandom</td></tr>
<tr><td>Hip-Fire Only</td><td>pwfnhipfire</td></tr>
<tr><td>Viewer supplies a custom challenge</td><td>pwfncustom</td></tr>
<tr><td>First Gun Only</td><td>pwfnfirstgun</td></tr>
<tr><td>No High Ground</td><td>pwfnnohigh</td></tr></table>
<p>Link the rewards to your own Twitch channel. Keep viewer text required for custom challenge. Choose who can use the commands and their cooldowns in Lumia.</p>
<p>Each reward has one Code action: it selects the challenge once, sends the resolved result to <code>FN_Challenge</code> and posts one Twitch chat message. Both displays show that result. Do not add a second random-roll or chat action.</p>
<h3>Chat controls</h3>
<p><b>!clearchallenge</b> dismisses the current challenge and advances to the next queued challenge. Add one Send Custom Overlay Content action with Code ID <code>FN_Challenge</code>:</p>
<pre>{"cmd":"clear"}</pre>
<p><b>!resetchallenge</b> clears the current challenge and the entire queue. Use the same Code ID:</p><pre>{"cmd":"reset"}</pre>
<p>A controller shortcut can send the same clear payload. Select one audible display if both overlay formats are open.</p>
<p>The landscape alert import includes <b>clearchallenge</b> and <b>resetchallenge</b>, plus five <b>disabled</b> chat helpers: <code>pwfnrandom</code>, <code>pwfnhipfire</code>, <code>pwfncustom</code>, <code>pwfnfirstgun</code> and <code>pwfnnohigh</code>. Keep those helpers disabled as chat commands; copy each helper's single Code action into your own Twitch Points reward. This avoids providing a free chat shortcut around your points rewards. Use your Lumia chat prefix (normally !) for the two controls. Review imported permissions and cooldowns. Your own reward bindings are manual; a separate-account reward import still needs checking.</p>
)PW"));
	topic("Stingers", QStringLiteral(R"PW(
<h2>Choose transitions between Stages</h2>
<p>Create or download a stinger at <a href="https://profounder.com/transitions/">Profounder</a>, which offers free and paid designs. Use a video you have permission to use.</p>
<ol><li>On page 3, open a stage group and expand <b>Stage transitions (optional)</b>. In <b>When entering this stage</b>, choose an existing stinger, Fade or Cut, or select <b>Add stinger…</b> for landscape or portrait. The Profounder button is here beside these choices.</li>
<li>Give a new stinger a recognizable name, choose its video file and enter the cut point in milliseconds. The cut point is when the video covers the screen and the next stage should appear. Use the timing supplied with the video.</li>
<li>The new stinger is selected for that stage and canvas. Click <b>Use an existing stinger…</b> to reuse it in another stage, or add a matching video for the other orientation. Select it only where you want the transition.</li>
<li>New stingers are added when you choose <b>Create my show</b>. Keep the video at the selected location. With outputs stopped, preview and adjust it in <b>Camera → Scene Transitions</b>. If the panel is hidden, show it through <b>Studio → Docks → Scene Transitions</b>. Change stage assignments later in <b>Show Control → Show → Manage stages</b>.</li></ol>
<p>A stinger changes between Stages. Pulse Weaver Move handles movement between Looks inside a Stage. Fade and Cut are available when you do not use a stinger.</p>
)PW"));
	return page;
}

inline void openDialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("Lumia Stream setup · Pulse Weaver");
	dialog.resize(900, 700);
	if (const auto *screen = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
		const auto available = screen->availableGeometry().size();
		dialog.resize(qMin(900, available.width() - 40), qMin(700, available.height() - 60));
	}
	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(createPage(&dialog), 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

} // namespace PulseInstructions
