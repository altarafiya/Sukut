import java.lang.reflect.*;
import java.util.*;

/** Read-only shell diagnostic; not included in the APK. Never records audio. */
public final class CaptureRoutingProbe {
    public static void main(String[] args) {
        try { inspect(); } catch (Throwable error) {
            System.out.println("diagnostic_error=" + error);
            if (error.getCause() != null) System.out.println("cause=" + error.getCause());
        }
    }
    private static void inspect() throws Exception {
        Class<?> runtime = Class.forName("dalvik.system.VMRuntime");
        Object vm = runtime.getDeclaredMethod("getRuntime").invoke(null);
        runtime.getDeclaredMethod("setHiddenApiExemptions", String[].class).invoke(vm, (Object)new String[]{""});
        Class<?> system = Class.forName("android.media.AudioSystem");
        for(Method method:system.getDeclaredMethods())if(method.getName().contains("RoutingUpdate") || method.getName().contains("RecordingCallback"))System.out.println("callback_api="+method);
        Class<?> cb=Class.forName("android.media.AudioSystem$AudioRecordingCallback"); for(Method m:cb.getDeclaredMethods())System.out.println("callback_method="+m); for(Field f:system.getDeclaredFields())if(f.getName().toLowerCase().contains("recording"))System.out.println("callback_field="+f);
        List<Object> mixes = new ArrayList<>();
        Object result = system.getDeclaredMethod("getRegisteredPolicyMixes", List.class).invoke(null, mixes);
        System.out.println("registered_mix_query=" + result + "; count=" + mixes.size());
        for (Object mix : mixes) {
            Class<?> type = mix.getClass();
            System.out.println("mix flags=" + type.getMethod("getRouteFlags").invoke(mix)
                + " registration=" + type.getMethod("getRegistration").invoke(mix));
            Method getter = type.getDeclaredMethod("getRule"); getter.setAccessible(true);
            Object rule = getter.invoke(mix);
            if(rule==null){System.out.println("  native descriptor: rules omitted");continue;}
            System.out.println("  rule_type="+rule.getClass().getName());
            for (Object criterion : (List<?>)rule.getClass().getMethod("getCriteria").invoke(rule)) {
                if(criterion==null){System.out.println("  criterion=null");continue;}
                Class<?> c = criterion.getClass();
                System.out.println("  rule=" + c.getMethod("getRule").invoke(criterion)
                    + "; int=" + c.getMethod("getIntProp").invoke(criterion));
            }
        }
    }
}
