/* -*- Mode: Vala; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * libgdadata
 * Copyright (C) Daniel Espinosa Ortiz 2011 <esodan@gmail.com>
 * 
 * libgda is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * libgda is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

using Gee;
using Gda;

namespace GdaData
{
	public class Table : Object, DbObject, DbNamedObject, DbTable<Value?>
	{
		protected DbRecordCollection _records;
		protected HashMap<string,DbFieldInfo<Value?>> _fields = new HashMap<string,DbFieldInfo<Value?>> ();
		protected HashMap<string,DbTable<Value?>> _fk_depends = new HashMap<string,DbTable<Value?>> ();
		protected HashMap<string,DbTable<Value?>> _fk = new HashMap<string,DbTable<Value?>> ();
		
		public Table.with_fields_info (HashMap<string,DbFieldInfo<Value?>> fields)
		{
			foreach (DbFieldInfo<Value?> f in fields.values) {
				_fields.set (f.name, f);
			}
		}
		// DbObject Interface
		public Connection connection { get; set; }
		public void update () throws Error {}
		public void save () throws Error {}
		public void append () throws Error {}
		// DbNamedObject Interface
		public string name { get; set; }
		
		// DbTable Interface
		public Collection<DbFieldInfo<Value?>> fields { 
			owned get { return _fields.values; } 
		}
		public DbSchema schema { get; set construct; }
		public Collection<DbRecord<Value?>> records { 
			owned get  {
				var q = new Gda.SqlBuilder (SqlStatementType.SELECT);
				q.set_table (name);
				q.select_add_field ("*", null, null);
				var s = q.get_statement ();
	    		var m = this.connection.statement_execute_select (s, null);
				_records = new RecordCollection (m, this);
				return _records;
			}
		}
		public Collection<DbTable<Value?>> fk_depends { owned get { return _fk_depends.values; } }
		public Collection<DbTable<Value?>> fk { owned get { return _fk.values; } }
	}
}