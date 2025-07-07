import lldb
import datetime
import sys

def __lldb_init_module(debugger, internal_dict):
    # This function is called when the script is imported.
    # The actual type summary registration will be done via launch.json for direct debugging.
    print("TimePoint pretty printer script loaded.", file=sys.stderr)

def TimePoint_SummaryProvider(valobj, internal_dict):
    try:
        print(f"TimePoint_SummaryProvider called for: {valobj.GetName()} ({valobj.GetTypeName()})", file=sys.stderr)
        # Get the duration part of the time_point
        duration_val = valobj.GetChildMemberWithName('__d_')
        if not duration_val or not duration_val.IsValid():
            print(f"Error: __d_ not found or invalid for {valobj.GetName()}", file=sys.stderr)
            return "<invalid duration>"

        # Extract the value (microseconds)
        microseconds_val = duration_val.GetChildMemberWithName('__rep_')
        if not microseconds_val or not microseconds_val.IsValid():
            print(f"Error: __rep_ not found or invalid for {valobj.GetName()}", file=sys.stderr)
            return "<invalid __rep_>"

        microseconds = microseconds_val.GetValueAsUnsigned(0)
        print(f"Extracted raw value (microseconds): {microseconds}", file=sys.stderr)

        # Convert microseconds to seconds
        seconds = microseconds / 1_000_000.0

        # Convert to datetime object (Unix epoch)
        dt_object = datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc)

        # Format the datetime object to a readable string
        formatted_time = dt_object.strftime("%Y-%m-%d %H:%M:%S")
        print(f"Formatted time: {formatted_time}", file=sys.stderr)
        return formatted_time
    except Exception as e:
        print(f"Error in TimePoint_SummaryProvider: {e}", file=sys.stderr)
        return f"<pretty print error: {e}>" 