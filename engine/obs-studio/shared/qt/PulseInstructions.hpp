#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
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
<ol><li><a href="https://www.lumiastream.com/downloads">Install Lumia Stream</a> and connect its Pulse Weaver plugin to the installation you are using.</li>
<li>Import the templates needed by your chosen Looks into Lumia. H is landscape and V is portrait. Customize the text, logo, colours and artwork in Lumia.</li>
<li>Use each imported overlay's chain / copy-link button to get your own browser link. On page 4, assign MAIN to Countdown, READY to Getting ready, BRB to Be right back and END to Ending, using each Look's separate landscape and portrait artwork questions. A template share link imports a template; it is not the browser link.</li>
<li>Add any accompanying commands once. Both canvas versions can use the same command set. Select your own Twitch rewards and Pulse Weaver Stages / Looks after importing.</li></ol>
<p>Use the shared alert overlay for alerts and the Fortnite widget. Add optional chat, camera frames and Newsroom only when your chosen Looks use them.</p>
<h3>Pulse Weaver template import links</h3>
<p>These Lumia share links import the matching template into your own library. Choose the pairs your Looks use:</p>
<table cellspacing="8"><tr><th align="left">Template pair</th><th>Landscape</th><th>Portrait</th></tr><tr><td>Starting countdown</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b4f-50b6-4733-a26c-e8610557564d">PW-MAIN01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b68-98a4-426a-b4d3-9b073f539d90">PW-MAIN01-V</a></td></tr><tr><td>Getting ready</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6a-264d-442f-a79d-59cd0be76fa7">PW-READY01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6b-f146-4b08-82a4-f31284fbf23a">PW-READY01-V</a></td></tr><tr><td>Be right back</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6d-3094-478b-8fb7-a8016dfc9cbe">PW-BRB01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b6e-c0bd-4dcf-a9bb-a7f036cc58cf">PW-BRB01-V</a></td></tr><tr><td>Ending + Credits</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b76-7ac2-4f2a-96bb-1c150ef6c7f8">PW-END01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38b7c-a5a9-4be1-8aa6-d83f9f10f94c">PW-END01-V</a></td></tr><tr><td>Shared alerts + Fortnite</td><td><a href="https://api.lumiastream.com/shared/download/a2f38b7e-f534-4553-add7-cd2644a64489">PW-ALERT01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38ba7-de88-4b45-a79c-068c63182298">PW-ALERT01-V</a></td></tr><tr><td>Newsroom / news intro</td><td><a href="https://api.lumiastream.com/shared/download/a2f38ba9-478c-4d1b-880a-d1953294ef2a">PW-NEWS01-H</a></td><td><a href="https://api.lumiastream.com/shared/download/a2f38baa-cc3f-4f42-b629-6e26b50d60bc">PW-NEWS01-V</a></td></tr></table>
<p>When importing <b>PW-ALERT01-H</b>, accept its accompanying command bundle once. The portrait copy has no duplicate commands. See Fortnite challenges for the reward setup.</p>
<h3>Newsroom and news intro</h3>
<p>For Newsroom, add NEWS as an extra overlay on page 4 and choose the specific Look that should show it. Add the matching H/V copies to their respective canvas slots. Use a group or all Looks only when you want the overlay throughout those presentations. The shared ALERT overlay can target several Looks through a stage group.</p>
<p>Newsroom is visible on load by default. For an event-only News intro, import a separate copy in Lumia and turn off <b>Show persistent Newsroom on load</b>. Its Code ID is <code>PW-NEWS01</code>. A news payload plays the intro and bulletin; a stop payload clears it:</p>
<pre>{"type":"news","title":"Your bulletin","message":"Your update"}
{"type":"stop"}</pre>
<p>Customize the logo, sound, desk, title and ticker in Lumia. Mute your portrait browser source when both formats are open, so alerts, HUDFX and TTS have one audible owner. Optional chat and camera frames use your own sources; they are not additional templates in this list.</p>
<p><a href="https://lumiastream.com/services/overlays">About Lumia overlays</a> &nbsp; | &nbsp;
<a href="https://forum.lumiastream.com/t/lumia-guides-photo-time/1285">Lumia's command import guide</a></p>
)PW"));
	topic("Starting countdown", QStringLiteral(R"PW(
<h2>Show Starting and start the timer</h2>
<p>These steps apply to imported Lumia artwork. If you selected <b>Built-in countdown</b>, its duration comes from the wizard instead; the Lumia payload below does not control that built-in page.</p>
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
<p>The overlay can call a Lumia command using its <b>goLiveCommand</b> setting when <b>autoFireGoLive</b> is enabled. The share copies leave automatic completion off and the command name blank. Your existing setup may use <code>go-live</code>. Configure your own completion command to select your desired next Stage / Look, or leave automatic completion off and switch manually.</p>
<p><b>Enable automatic completion in only one overlay instance.</b> Disable it in the other orientation and any extra display copies so the follow-on command runs once. Reopening or refreshing a timer browser during countdown needs a restart check.</p>
<p>Starting the broadcast is a separate optional Start Show / Start Platform action. Configure your destinations before adding it.</p>
<p>These instructions match the existing <code>start-screen</code> payload. A portable native command import and the complete sequence in a newly generated show still require verification.</p>
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
<ol><li>In Studio's Scene Transitions, add a Stinger, choose its video and set the transition point.</li>
<li>Give it a recognizable name. Add separate landscape and portrait stingers when you use both formats.</li>
<li>Select the installed transition for each Stage and canvas in the wizard. You can change it later in Manage stages.</li></ol>
<p>A stinger changes between Stages. Pulse Weaver Move handles movement between Looks inside a Stage. Fade and Cut are available when you do not use a stinger.</p>
)PW"));
	return page;
}

inline void openDialog(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle("Lumia Stream setup · Pulse Weaver");
	dialog.resize(900, 700);
	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(createPage(&dialog), 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

} // namespace PulseInstructions
