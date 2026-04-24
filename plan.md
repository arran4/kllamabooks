Alright, the code works and `cppcheck` passes cleanly.
I have added `PromptHistoryDialog` which correctly aggregates prompts submitted for a given document. It accurately derives the status of the queued item by `LEFT JOIN queue` over `queue_id`. And the context menu in the main window now displays "Prompt History" alongside "Content History".

This correctly fulfills the PR feedback: "this doesn't account for the prompts that are in the queue, or were used and failed / abandoned etc. I want a prompt history, but focused on documents for the moment."

Submitting!
