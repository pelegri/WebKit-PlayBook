# Copyright (C) 2010 Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from datetime import datetime
import logging

from google.appengine.ext import db

from model.datastorefile import DataStoreFile


class TestFile(DataStoreFile):
    builder = db.StringProperty()
    test_type = db.StringProperty()

    @classmethod
    def delete_file(cls, key, builder, test_type, name, limit):
        if key:
            file = db.get(key)
            if not file:
                logging.warning("File not found, key: %s.", key)
                return False

            file._delete_all()
        else:
            files = cls.get_files(builder, test_type, name, limit)
            if not files:
                logging.warning(
                    "File not found, builder: %s, test_type:%s, name: %s.",
                    builder, test_type, name)
                return False

            for file in files:
                file._delete_all()

        return True

    @classmethod
    def get_files(cls, builder, test_type, name, load_data=True, limit=1):
        query = TestFile.all()
        if builder:
            query = query.filter("builder =", builder)
        if test_type:
            query = query.filter("test_type =", test_type)
        if name:
            query = query.filter("name =", name)

        files = query.order("-date").fetch(limit)
        if load_data:
            for file in files:
                file.load_data()

        return files

    @classmethod
    def add_file(cls, builder, test_type, name, data):
        file = TestFile()
        file.builder = builder
        file.test_type = test_type
        file.name = name

        if not file.save(data):
            return None

        logging.info(
            "File saved, builder: %s, test_type: %s, name: %s, key: %s.",
            builder, test_type, file.name, str(file.data_keys))

        return file

    @classmethod
    def update(cls, builder, test_type, name, data):
        files = cls.get_files(builder, test_type, name)
        if not files:
            return cls.add_file(builder, test_type, name, data)

        file = files[0]
        if not file.save(data):
            return None

        logging.info(
            "File replaced, builder: %s, test_type: %s, name: %s, data key: %s.",
            builder, test_type, file.name, str(file.data_keys))

        return file

    def save(self, data):
        if not self.save_data(data):
            return False

        self.date = datetime.now()
        self.put()

        return True

    def _delete_all(self):
        self.delete_data()
        self.delete()
