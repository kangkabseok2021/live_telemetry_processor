// ImagingEngineClient.cs
// C# P/Invoke wrapper for libimaging_engine.so / imaging_engine.dll
// No .csproj required — standalone file for reference / copy-paste into a project.
//
// Usage:
//   using var client = new ImagingEngineClient();
//   var result = client.SendEvent(EventId.CmdStart);
//   ImagingState state = client.GetState();

using System;
using System.Runtime.InteropServices;

namespace ImagingEngine
{
    // ── Native constants (mirror of c_api.h) ──────────────────────────────

    /// <summary>Events that can be sent to the state machine via P/Invoke.</summary>
    public enum EventId : int
    {
        /// <summary>Start calibration sequence.</summary>
        CmdStart     = 1,
        /// <summary>Stop acquisition.</summary>
        CmdStop      = 2,
        /// <summary>Reset from Fault state to Idle.</summary>
        CmdReset     = 3,
        /// <summary>A frame is ready in the acquisition buffer.</summary>
        EvtFrame     = 4,
        /// <summary>An error has occurred; transitions to Fault.</summary>
        EvtError     = 5,
        /// <summary>Calibration sequence has completed.</summary>
        EvtCalibDone = 6,
        /// <summary>A batch of frames is ready for processing.</summary>
        EvtBatch     = 7,
        /// <summary>Batch processing has completed.</summary>
        EvtProcDone  = 8,
    }

    /// <summary>State identifiers returned by <see cref="ImagingEngineClient.GetState"/>.</summary>
    public enum ImagingState : int
    {
        /// <summary>Engine is idle; awaiting CmdStart.</summary>
        Idle        = 0,
        /// <summary>Calibration sequence in progress.</summary>
        Calibrating = 1,
        /// <summary>Acquiring frames from the sensor.</summary>
        Acquiring   = 2,
        /// <summary>Processing a batch of acquired frames.</summary>
        Processing  = 3,
        /// <summary>A fault has been detected; awaiting CmdReset.</summary>
        Fault       = 4,
    }

    /// <summary>Return codes from <see cref="ImagingEngineClient.SendEvent"/>.</summary>
    public enum TransitionResult : int
    {
        /// <summary>Transition succeeded.</summary>
        Ok                  =  0,
        /// <summary>The event is not valid in the current state.</summary>
        InvalidTransition   =  1,
        /// <summary>General error (null handle, unknown event, etc.).</summary>
        Error               = -1,
    }

    // ── P/Invoke declarations ─────────────────────────────────────────────

    internal static class NativeMethods
    {
#if WINDOWS
        private const string LibName = "imaging_engine.dll";
#else
        private const string LibName = "libimaging_engine.so";
#endif

        /// <summary>Allocate a new state machine handle.</summary>
        [DllImport(LibName, EntryPoint = "smachine_create",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr Create();

        /// <summary>Send an event to the state machine. Returns a <see cref="TransitionResult"/> code.</summary>
        [DllImport(LibName, EntryPoint = "smachine_send_event",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern int SendEvent(IntPtr handle, int eventId);

        /// <summary>Return current state index (maps to <see cref="ImagingState"/>).</summary>
        [DllImport(LibName, EntryPoint = "smachine_get_state",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern int GetState(IntPtr handle);

        /// <summary>Return cumulative count of RT deadline violations.</summary>
        [DllImport(LibName, EntryPoint = "smachine_get_deadline_violations",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong GetDeadlineViolations(IntPtr handle);

        /// <summary>Free the state machine handle.</summary>
        [DllImport(LibName, EntryPoint = "smachine_destroy",
                   CallingConvention = CallingConvention.Cdecl)]
        internal static extern void Destroy(IntPtr handle);
    }

    // ── SafeHandle wrapper ────────────────────────────────────────────────

    /// <summary>
    /// Wraps the native <c>smachine_t*</c> opaque pointer in a
    /// <see cref="SafeHandle"/> so the GC releases it on finalisation.
    /// </summary>
    internal sealed class SmachineSafeHandle : SafeHandle
    {
        /// <summary>Initialises an empty (invalid) handle.</summary>
        public SmachineSafeHandle() : base(IntPtr.Zero, ownsHandle: true) { }

        /// <inheritdoc/>
        public override bool IsInvalid => handle == IntPtr.Zero;

        /// <inheritdoc/>
        protected override bool ReleaseHandle()
        {
            NativeMethods.Destroy(handle);
            return true;
        }
    }

    // ── High-level client ─────────────────────────────────────────────────

    /// <summary>
    /// Managed wrapper around the C imaging engine state machine.
    /// Implements <see cref="IDisposable"/>; use in a <c>using</c> block to
    /// ensure the native handle is released promptly.
    /// </summary>
    /// <example>
    /// <code>
    /// using var engine = new ImagingEngineClient();
    /// var rc = engine.SendEvent(EventId.CmdStart);
    /// Console.WriteLine($"State: {engine.GetState()}");
    /// </code>
    /// </example>
    public sealed class ImagingEngineClient : IDisposable
    {
        private readonly SmachineSafeHandle _handle;
        private bool _disposed;

        /// <summary>
        /// Creates a new imaging engine instance by calling <c>smachine_create</c>.
        /// </summary>
        /// <exception cref="InvalidOperationException">
        /// Thrown if the native library returns a null handle.
        /// </exception>
        public ImagingEngineClient()
        {
            _handle = new SmachineSafeHandle();
            IntPtr raw = NativeMethods.Create();
            if (raw == IntPtr.Zero)
                throw new InvalidOperationException(
                    "smachine_create returned null — check that libimaging_engine is loaded.");
            _handle.SetHandle(raw);
        }

        /// <summary>
        /// Send an event to the state machine.
        /// </summary>
        /// <param name="event">The event to dispatch.</param>
        /// <returns>
        /// <see cref="TransitionResult.Ok"/> if the transition succeeded,
        /// <see cref="TransitionResult.InvalidTransition"/> if the event is
        /// not valid in the current state, or
        /// <see cref="TransitionResult.Error"/> on a general failure.
        /// </returns>
        public TransitionResult SendEvent(EventId @event)
        {
            ThrowIfDisposed();
            int code = NativeMethods.SendEvent(_handle.DangerousGetHandle(), (int)@event);
            return (TransitionResult)code;
        }

        /// <summary>Returns the current state of the imaging engine.</summary>
        public ImagingState GetState()
        {
            ThrowIfDisposed();
            int idx = NativeMethods.GetState(_handle.DangerousGetHandle());
            return (ImagingState)idx;
        }

        /// <summary>
        /// Gets the cumulative count of real-time deadline violations
        /// (transitions that exceeded <c>DEADLINE_US = 500 µs</c>).
        /// </summary>
        public ulong DeadlineViolations
        {
            get
            {
                ThrowIfDisposed();
                return NativeMethods.GetDeadlineViolations(_handle.DangerousGetHandle());
            }
        }

        /// <inheritdoc/>
        public void Dispose()
        {
            if (!_disposed)
            {
                _handle.Dispose();
                _disposed = true;
            }
        }

        private void ThrowIfDisposed()
        {
            if (_disposed)
                throw new ObjectDisposedException(nameof(ImagingEngineClient));
        }
    }
}
