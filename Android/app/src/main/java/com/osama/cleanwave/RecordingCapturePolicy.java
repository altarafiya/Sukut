package com.osama.cleanwave;

import java.util.*;
import java.util.regex.*;

/** Parses only known, usage-based playback captures; unfamiliar rules stay untouched. */
final class RecordingCapturePolicy {
    static final class Snapshot {
        final String address;
        final List<Integer> usages;
        final Integer uid;
        Snapshot(String address, Collection<Integer> usages, Integer uid) {
            this.address = address;
            this.uid=uid;
            this.usages = Collections.unmodifiableList(new ArrayList<>(new TreeSet<>(usages)));
        }
    }
    private static final Map<String,Integer> USAGES = new HashMap<>();
    static {
        String[] names = {"UNKNOWN","MEDIA","VOICE_COMMUNICATION","VOICE_COMMUNICATION_SIGNALLING",
            "ALARM","NOTIFICATION","NOTIFICATION_TELEPHONY_RINGTONE","NOTIFICATION_COMMUNICATION_REQUEST",
            "NOTIFICATION_COMMUNICATION_INSTANT","NOTIFICATION_COMMUNICATION_DELAYED","NOTIFICATION_EVENT",
            "ASSISTANCE_ACCESSIBILITY","ASSISTANCE_NAVIGATION_GUIDANCE","ASSISTANCE_SONIFICATION","GAME",
            "VIRTUAL_SOURCE","ASSISTANT","CALL_ASSISTANT"};
        for (int i=0;i<names.length;i++) USAGES.put("AUDIO_USAGE_"+names[i], i);
        USAGES.put("AUDIO_USAGE_NOTIFICATION_RINGTONE",6);
    }
    private static final Pattern HEADER = Pattern.compile("\\s*Audio Policy Mix \\d+.*:");
    private static final Pattern USAGE = Pattern.compile(".*Criterion \\d+: RULE_MATCH_ATTRIBUTE_USAGE (AUDIO_USAGE_\\w+)\\s*");
    private static final Pattern UID = Pattern.compile(".*Criterion \\d+: RULE_MATCH_UID (\\d+)\\s*");

    static Map<String,Snapshot> parse(String dump) {
        Map<String,Snapshot> result = new LinkedHashMap<>();
        String address=null; boolean section=false, players=false, capture=false, valid=true;
        Integer uid=null;
        List<Integer> usages=new ArrayList<>();
        for (String raw : (dump+"\nAudio sources (end):").split("\n")) {
            String line=raw.trim();
            boolean header=HEADER.matcher(line).matches();
            boolean end=line.trim().startsWith("Audio sources") || line.trim().startsWith("AllowedCapturePolicies:");
            if(header || end) {
                if(section && valid && players && capture && address!=null && !address.isEmpty() && usages.contains(1)) result.put(address,new Snapshot(address,usages,uid));
                section=header; players=false; capture=false; valid=true; address=null; uid=null; usages.clear();
                continue;
            }
            if(!section) continue;
            if(line.contains("- mix type:")) players=line.contains("MIX_TYPE_PLAYERS");
            else if(line.contains("- Route Flags:")) capture=line.contains("MIX_ROUTE_FLAG_RENDER") && line.contains("MIX_ROUTE_FLAG_LOOP_BACK");
            else if(line.contains("- device address:")) address=line.substring(line.indexOf("- device address:")+17).trim();
            else if(line.contains("Criterion ")) {
                Matcher match=USAGE.matcher(line);
                Integer usage=match.matches()?USAGES.get(match.group(1)):null;
                Matcher uidMatch=UID.matcher(line);
                if(usage!=null) usages.add(usage);
                else if(uidMatch.matches() && uid==null) {
                    try {uid=Integer.valueOf(uidMatch.group(1));}catch(NumberFormatException unsupported){valid=false;}
                } else valid=false;
            }
        }
        return result;
    }
}
