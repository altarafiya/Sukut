package com.osama.cleanwave;

import java.lang.reflect.InvocationTargetException;

/** Keep framework reflection wrappers from hiding the actionable routing error. */
public final class WorkerErrors {
    private WorkerErrors() { }
    public static String concise(Throwable problem) {
        Throwable current = problem;
        for (int depth = 0; depth < 8 && current instanceof InvocationTargetException; depth++) {
            Throwable cause = current.getCause();
            if (cause == null || cause == current) break;
            current = cause;
        }
        String message = current.getMessage();
        return current.getClass().getSimpleName() + (message == null || message.isEmpty() ? "" : ": " + message);
    }
}
