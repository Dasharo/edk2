#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.<BR>
    def __init__(self, subject, message):
        self.check_signed_off_by()
        self.check_misc_signatures()
        self.check_overall_format()
                not lines[i].startswith('git-svn-id:')):
                if self.filename.endswith('.sh'):
                if self.filename == '.gitmodules':
                    # .gitmodules is updated by git and uses tabs and LF line
                    # endings.  Do not enforce no tabs and do not enforce
                    # CR/LF line endings.
        if self.force_crlf and eol != '\r\n':
        msg_check = CommitMessageCheck(self.commit_subject, self.commit_msg)