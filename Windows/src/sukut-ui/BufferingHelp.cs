using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class ShellView
{
    private StackPanel BuildBufferingHelp()
    {
        var body = new StackPanel { Spacing = 10 };
        body.Children.Add(HelpCard(null, "\uE8F1", "SukutAccentBrush", L("What does buffering do?", "ما هو التخزين المؤقت؟"),
            L("Sukut keeps a short queue of processed sound ready to play. This helps sound stay smooth when your PC briefly gets busy.", "يُبقي سُكوت كمية قصيرة من الصوت المعالَج جاهزة للتشغيل، ليبقى الصوت سلسًا عندما ينشغل الحاسوب قليلًا.")));
        body.Children.Add(HelpCard(null, "\uE945", "GainRaisedBrush", L("Lowest · less waiting", "الأقل · انتظار أقصر"),
            L("Adds the least buffering delay. Choose it if playback stays smooth.", "يضيف أقل تأخير للتخزين المؤقت. اختره إذا استمر الصوت بسلاسة.")));
        body.Children.Add(HelpCard(null, "\uE9E9", "ModeOptimizedBrush", L("Balanced · start here", "متوازن · ابدأ به"),
            L("A little more delay, with more protection against short pauses. A good everyday choice.", "تأخير أكبر قليلًا مقابل حماية أفضل من التقطّع القصير. خيار مناسب للاستخدام اليومي.")));
        body.Children.Add(HelpCard(null, "\uE823", "ModeStrictBrush", L("Maximum · fewer interruptions", "الأكبر · انقطاعات أقل"),
            L("Keeps more sound ready. Try it if audio still cuts out; it adds the most buffering delay.", "يُبقي صوتًا أكثر جاهزًا. جرّبه إذا استمر التقطّع؛ لكنه يضيف أكبر تأخير للتخزين المؤقت.")));
        body.Children.Add(HelpCard(null, "\uE714", "SukutAccentBrush", L("Audio delay and video sync", "تأخير الصوت وتزامن الفيديو"),
            L("Delay means hearing a sound after the action that caused it. For example, lips move before you hear the words. This mismatch is called desync.", "التأخير هو سماع الصوت بعد الحدث الذي سبّبه، مثل تحرّك الشفاه قبل سماع الكلام. يُسمّى هذا عدم التزامن."),
            L("More buffering can increase that mismatch. Use the lowest setting that plays smoothly. Music removal itself also takes time; Lowest does not mean zero delay.", "قد يزيد التخزين المؤقت هذا الفارق. استخدم أقل خيار يعمل بسلاسة. إزالة الموسيقى نفسها تستغرق وقتًا أيضًا؛ لذا فالأقل لا يعني انعدام التأخير.")));
        return body;
    }
}
